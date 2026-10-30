"""Boot ONL from an Intel IMSM (VROC) RAID.

The module is part of the ONL common code and runs in the loader and
in the runtime system. Nothing is declared by a platform: the loader
stage probes the disks itself and takes the RAID path only when the
ONL partitions sit on RAID member disks. A platform without RAID
member disks leaves the loader stage at once, without touching any
block device, so its boot does not change.

Supported layout: one IMSM container with all of its member disks
present, holding one or more volumes of level RAID0 or RAID1. The
volume that carries the four ONL partition labels is
the system volume; every volume of the container is assembled and
goes through the same read-only / read-write handling, so that the
runtime system can use the other volumes too. A degraded volume, a
level change in progress, or a kernel without the personality of a
volume stops the boot.

Stages, in boot order:

  loader        sysinit, before fsck and mount: assemble read-only,
                verify, start mdmon, wait for resync
  prepare-root  boot, before the rootfs switch: unmount, read-only
  resume-root   switchroot (PID 1): restart mdmon, remount the root
  takeover      /etc/boot.d/05.mdraid: the runtime mdmon takes over

The rootfs handoff (prepare-root and resume-root) knows two kinds of
/newroot, recorded as the handoff mode:

  md       BOOTMODE=INSTALLED: /newroot is the ext4 ONL-DATA of the
           system volume. It is unmounted with the other MD
           filesystems and mounted again after the process cleanup.
  mounted  BOOTMODE=SWI: /newroot is the overlay that 'swiprep
           --overlay' builds in RAM (squashfs on a loop device over
           /tmp/.rootfs, tmpfs upper layer), or an NFS rootfs. It
           does not live on the RAID and stays mounted; only the MD
           filesystems are unmounted. The overlay layers are checked
           before anything is unmounted, because a layer on an MD
           volume would keep the volume busy after the unmounts.

Level differences that the stages take into account (mdadm 4.3):
mdmon does not manage RAID0 volumes, "mdadm --wait-clean" returns at
once for them, "mdadm --readwrite" makes them active itself, and
they have no degraded, sync_action or sync_completed attributes.

The kernel command line option onl_raid=off (ONL writes it to
/etc/onl/raid) disables the loader stage for one boot. The label
lookup of onl.blockdev still refuses RAID member disks, so ONL will
not mount a member by mistake even then.
"""

from __future__ import print_function

import glob
import json
import os
import re
import signal
import stat
import subprocess
import sys
import time

import onl.blockdev

# Static mdadm and mdmon from the onl-mdadm package, installed under
# their usual names; the loader and the runtime system use the same
# files. The stages call them by this full path, never through PATH,
# so the Debian release's mdadm version plays no part in the boot.
MDADM_DIR = '/sbin'
SYS_BLOCK = '/sys/class/block'
SYS_DEV_BLOCK = '/sys/dev/block'
RUN_DIR = '/run/onl'
STATE = RUN_DIR + '/mdraid.state'
DEVICE_MAP = RUN_DIR + '/block-devices.json'
# JSON written by prepare-root and read by resume-root: the handoff
# mode and the rootfs device (md) or mount description (mounted).
HANDOFF = RUN_DIR + '/mdraid.handoff'
LOG = '/etc/onl/mdraid.log'
# The squashfs of the SWI that 'swiprep --overlay' keeps in RAM and
# mounts through a loop device as the lower layer of /newroot.
SWI_ROOTFS = '/tmp/.rootfs'
# Where /bin/boot mounts the rootfs that switch_root enters, and where
# swiprep mounts the two layers of the overlay for BOOTMODE=SWI.
NEWROOT = '/newroot'
SWI_LOWER = NEWROOT + '.lower'
SWI_UPPER = NEWROOT + '.upper'
# Filesystems that live in RAM; the squashfs file must be on one of them.
RAM_FILESYSTEMS = ('tmpfs', 'ramfs', 'rootfs')
# Written by sysinit from the kernel command line option onl_raid=.
MODE_FILE = '/etc/onl/raid'
# The sysvinit 'sendsigs' script does not signal the PIDs listed here.
OMIT_DIR = '/run/sendsigs.omit.d'
REQUIRED_LABELS = ('ONL-BOOT', 'ONL-CONFIG', 'ONL-IMAGES', 'ONL-DATA')
LABELS = REQUIRED_LABELS + ('EFI System', 'ONIE-BOOT', 'ACC-DIAG')
# IMSM level number (mdadm --examine) -> kernel level name (sysfs).
# Other IMSM levels (10, 5) are refused as unsupported; adding one
# here (and to REDUNDANT when it has redundancy) is all the module
# needs, the stages themselves are level independent.
LEVELS = {'0': 'raid0', '1': 'raid1'}
# Levels with redundancy: managed by mdmon, can resync and degrade.
REDUNDANT = ('raid1',)
# Whole disks that may hold an IMSM array: NVMe and SATA/SAS disks, and
# virtio disks for tests. Removable and virtual devices are skipped.
DISK_PATTERN = re.compile(r'^(nvme[0-9]+n[0-9]+|sd[a-z]+|vd[a-z]+)$')
# Allow 15 minutes without a visible resync progress update.
RESYNC_STALL = 900
# Resync speed limit used only while the loader waits, in KiB/s.
RESYNC_SPEED_MAX = 5000000
# Wait this long for member disks to appear, in one-second steps.
DISCOVER_ATTEMPTS = 11
# Values of onl_raid= that disable the loader stage.
OFF_VALUES = ('off', '0', 'no', 'false', 'disable', 'disabled')


def require(condition, message):
    """Stop the current boot stage when a required condition is not met."""
    if not condition:
        raise RuntimeError(message)


def read(path):
    """Read a small text file, including NUL-separated process arguments."""
    with open(path) as stream:
        return stream.read().strip()


def mkdir(path):
    """Create a directory without hiding other filesystem errors."""
    if not os.path.isdir(path):
        os.makedirs(path)


def atomic_write(path, text):
    """Publish a complete RAM state file for the next boot stage."""
    mkdir(os.path.dirname(path))
    with open(path + '.tmp', 'w') as stream:
        stream.write(text)
    os.rename(path + '.tmp', path)


def elapsed_seconds():
    """Read elapsed boot time without using the system clock."""
    return float(read('/proc/uptime').split()[0])


def console(message):
    """Log a message and show it before sysinit restores console output."""
    print(message)
    sys.stdout.flush()
    try:
        with open('/dev/console', 'w') as stream:
            stream.write(message + '\n')
    except (IOError, OSError):
        pass


def fields(text):
    """Parse key-value output and reject duplicate keys."""
    result = {}
    for line in text.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            require(key not in result, 'Duplicate field: ' + key)
            result[key] = value
    return result


def md_uuid(value):
    """Validate and normalize an MD UUID."""
    value = value.lower()
    require(re.match(r'^[0-9a-f]{8}(:[0-9a-f]{8}){3}$', value), 'Invalid MD UUID.')
    return value


def block_device(path):
    """Check the actual device node instead of trusting its name."""
    return os.path.exists(path) and stat.S_ISBLK(os.stat(path).st_mode)


def run(args, check=True):
    """Run an argument list and retain output and status on failure."""
    env = dict(os.environ, LC_ALL='C', MDADM_NO_MDMON='1', MDADM_NO_SYSTEMCTL='1')
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    output = process.communicate()[0].decode('utf-8', 'replace').strip()
    require(not check or process.returncode == 0,
            'Command failed (%s): %s\n%s' % (process.returncode, ' '.join(args), output))
    return process.returncode, output


def tool_path(name):
    """Return the path of one static tool from the onl-mdadm package."""
    require(name in ('mdadm', 'mdmon'), 'Expected mdadm or mdmon.')
    return MDADM_DIR + '/' + name


def tools_present():
    """Return True when the static mdadm and mdmon are installed."""
    return all(os.access(tool_path(name), os.X_OK) for name in ('mdadm', 'mdmon'))


def tool_command(name, args):
    """Build a command for a static tool.

    The loader and the runtime system both use these tools, so no boot
    stage depends on the mdadm version of the Debian release.
    """
    return [tool_path(name)] + list(args)


def mdadm(*args):
    """Run the static mdadm without automatic monitor or systemctl startup."""
    return run(tool_command('mdadm', args))[1]


def raid_mode():
    """Return the value of the kernel option onl_raid=, or None."""
    if not os.path.isfile(MODE_FILE):
        return None
    return read(MODE_FILE).lower()


def mount_path(value):
    """Decode octal escapes used for mountinfo path fields."""
    return re.sub(r'\\([0-7]{3})', lambda match: chr(int(match.group(1), 8)), value)


def mounts():
    """Read mount identities, roots and decoded paths from mountinfo."""
    result = []
    for line in read('/proc/self/mountinfo').splitlines():
        left, right = line.split(' - ', 1)
        parts = left.split()
        result.append((parts[2], mount_path(parts[3]), mount_path(parts[4]), right.split()[0]))
    return result


def mount_info(target):
    """Describe the one mount at a path: ID, device number, root, type and options.

    The options are the 'key=value' superblock options of the mount
    (an overlay shows its layers there). The result is what the
    handoff records for a rootfs that stays mounted, so resume-root
    can prove that the mount did not change during process cleanup.
    """
    result = []
    for line in read('/proc/self/mountinfo').splitlines():
        left, right = line.split(' - ', 1)
        parts, fs = left.split(), right.split()
        if mount_path(parts[4]) == target:
            result.append(dict(id=parts[0], number=parts[2], root=mount_path(parts[3]),
                               fs=fs[0], options=fields('\n'.join(fs[2].split(',')))))
    require(len(result) == 1, 'Missing or stacked mount: ' + target)
    return result[0]


def loop_backing_file(number):
    """Return the backing file of the loop device with this major:minor number."""
    device = os.path.realpath(SYS_DEV_BLOCK + '/' + number)
    require(re.match(r'^loop[0-9]+$', os.path.basename(device)),
            'The SWI squashfs is not on a loop device.')
    return read(device + '/loop/backing_file')


def partitions_of(name):
    """Return the sysfs directories of the partitions of a whole disk."""
    base = SYS_BLOCK + '/' + name
    return sorted(path for path in glob.glob(base + '/*')
                  if os.path.isfile(path + '/partition'))


def device_numbers(device):
    """Return a whole device and its partitions, indexed by major:minor."""
    name = os.path.basename(device)
    paths = [SYS_BLOCK + '/' + name] + partitions_of(name)
    return dict((read(path + '/dev'), '/dev/' + os.path.basename(path))
                for path in paths if os.path.isfile(path + '/dev'))


def no_holders(devices):
    """Reject members or MD partitions owned by another block device."""
    for device in devices:
        require(not glob.glob(SYS_BLOCK + '/' + os.path.basename(device) + '/holders/*'),
                'Device has a holder: ' + device)


def identity(text):
    """Parse 'mdadm --examine --scan' of a member: one container, its volumes.

    Returns a dict with container_uuid and volumes, a list of dicts
    with member (the subarray index) and uuid, sorted by member.
    """
    arrays = [fields('\n'.join(line.split()[1:])) for line in text.splitlines()
              if line.split()[:1] == ['ARRAY']]
    containers = [entry for entry in arrays if entry.get('metadata') == 'imsm']
    volumes = [entry for entry in arrays if 'container' in entry]
    require(len(arrays) >= 2 and len(containers) == 1 and len(volumes) == len(arrays) - 1,
            'Expected one IMSM container with at least one volume.')
    container_uuid = md_uuid(containers[0]['UUID'])
    result = []
    for volume in volumes:
        require(md_uuid(volume['container']) == container_uuid and
                re.match(r'^[0-9]+$', volume['member']), 'Invalid IMSM container relationship.')
        result.append(dict(member=volume['member'], uuid=md_uuid(volume['UUID'])))
    members = [volume['member'] for volume in result]
    require(len(set(members)) == len(members), 'Duplicate IMSM volume index.')
    return dict(container_uuid=container_uuid, volumes=sorted(result, key=lambda v: int(v['member'])))


def volume_levels(text):
    """Parse 'mdadm --examine' of a member: the level of every volume.

    Returns a dict keyed by volume UUID with the kernel level name and
    the member count of the volume. A level change in progress, an
    unknown level or a repeated UUID is refused.
    """
    result = {}
    current = None
    for line in text.splitlines():
        if re.match(r'^\[.*\]:$', line):
            current = {}
            continue
        if current is None or ':' not in line:
            continue
        key, value = [part.strip() for part in line.split(':', 1)]
        if key == 'UUID':
            uuid = md_uuid(value)
            require(uuid not in result, 'Duplicate IMSM volume UUID.')
            result[uuid] = current
        elif key == 'RAID Level':
            require('<--' not in value, 'A RAID level change is in progress.')
            require(value in LEVELS, 'Unsupported IMSM RAID level: ' + value)
            current['level'] = LEVELS[value]
        elif key == 'Members':
            require('<--' not in value and re.match(r'^[0-9]+$', value), 'Unexpected member count: ' + value)
            current['members'] = int(value)
    for uuid, info in result.items():
        require('level' in info and 'members' in info, 'Incomplete volume description: ' + uuid)
    return result


def personality(level):
    """Return the /proc/mdstat personality tag of a kernel level name."""
    return '[' + level + ']'


def candidate_disks():
    """Return the whole disks that may hold an IMSM array.

    Virtual devices (MD, device-mapper, loop, RAM) and removable media
    never take part, so a USB stick with old RAID metadata cannot
    change the boot of a platform.
    """
    disks = []
    for path in sorted(glob.glob(SYS_BLOCK + '/*')):
        name = os.path.basename(path)
        if not DISK_PATTERN.match(name):
            continue
        if '/devices/virtual/' in os.path.realpath(path):
            continue
        removable = path + '/removable'
        if os.path.isfile(removable) and read(removable) != '0':
            continue
        disks.append(name)
    return disks


def label_disks():
    """Return the whole disks that carry the ONL partition labels.

    MD devices are left out: before the array is assembled the labels
    are visible on the member disks, afterwards only on the MD volume.
    """
    disks = set()
    for label in REQUIRED_LABELS:
        for device in onl.blockdev.candidates(label):
            disk = onl.blockdev.whole_disk(device)
            if disk and not onl.blockdev.is_md(disk):
                disks.add(disk)
    return disks


def decide(members, suspects, holders):
    """Decide whether this boot must use the RAID.

    members  : disks that carry readable IMSM metadata
    suspects : disks whose RAID state could not be read, with reasons
    holders  : disks that carry the ONL partition labels

    Returns (True, reason) for a RAID boot and (False, reason) for the
    standard ONL boot. Raises RuntimeError when neither is safe.
    """
    members = set(members)
    holders = set(holders)
    unknown = sorted(holders & set(suspects))
    if unknown:
        raise RuntimeError('Cannot read the RAID state of the ONL disk(s): ' + '; '.join(
            '%s: %s' % (disk, suspects[disk]) for disk in unknown))
    if not members:
        return False, 'no RAID member disk found'
    if not holders:
        return True, 'RAID members present and no ONL partition visible yet'
    if holders <= members:
        return True, 'ONL is installed on the RAID members'
    if holders & members:
        raise RuntimeError('ONL partitions are on RAID members and on other disks: ' +
                           ' '.join(sorted(holders)))
    return False, 'RAID metadata on %s is not used by ONL' % ' '.join(sorted(members))


def expected_members(found):
    """Return the member count that the IMSM metadata of the found disks declares."""
    counts = set()
    for info in found.values():
        value = info.get('MD_DEVICES', '')
        if re.match(r'^[0-9]+$', value):
            counts.add(int(value))
    return max(counts) if counts else 0


class Raid(object):
    """Keep array identity local while executing the four ONL boot stages."""

    def __init__(self):
        """Read the platform selected by ONL, without a hard-coded model name."""
        self.platform = read('/etc/onl/platform')
        require(re.match(r'^[A-Za-z0-9][A-Za-z0-9_.-]*$', self.platform), 'Invalid ONL platform.')
        self.state = {}
        self.volumes = []

    # -- discovery -------------------------------------------------------

    def probe_disk(self, name):
        """Examine one whole disk; return its IMSM fields or None.

        Raises RuntimeError when the disk cannot be read or carries a
        RAID signature that mdadm cannot interpret.

        'mdadm --examine --export' answers in three ways:
          exit 0 with MD_* fields   RAID metadata (IMSM, or another
                                    kind that is refused here)
          exit 0 with no output     a plain MBR or GPT partition table:
                                    mdadm "examines" those too, but has
                                    nothing to export for them
          exit 1                    no superblock at all
        A plain disk gives one of the last two, so both go on to the
        blkid check for RAID signatures that mdadm does not know.
        """
        device = '/dev/' + name
        path = SYS_BLOCK + '/' + name
        require(block_device(device), 'Not a block device: ' + device)
        sectors = int(read(path + '/size'))
        require(sectors > 0, 'Empty device: ' + device)
        with open(device, 'rb') as stream:
            for offset in (0, (sectors - 1) * 512):
                stream.seek(offset)
                require(len(stream.read(512)) == 512, 'Short read: ' + device)
        rc, output = run(tool_command('mdadm', ['--examine', '--export', device]), check=False)
        require(rc in (0, 1), 'mdadm examination failed (%s): %s\n%s' % (rc, device, output))
        info = fields(output) if rc == 0 else {}
        if any(key.startswith('MD_') for key in info):
            require(info.get('MD_METADATA') == 'imsm', 'Unsupported RAID metadata: ' + device)
            return info
        status, signature = run(['blkid', '-p', '-o', 'export', device], check=False)
        require(status in (0, 2), 'Cannot identify signatures: ' + device)
        require('raid' not in fields(signature).get('TYPE', '').lower(),
                'mdadm cannot read the RAID signature: %s\n%s' % (device, output))
        return None

    def discover(self):
        """Probe the disks and decide whether this boot uses the RAID.

        Returns the IMSM members as a dict, or None for the standard
        boot. Disks that cannot be probed are only an error when ONL
        is installed on them, so an unrelated bad disk does not stop
        the boot of a platform without RAID.
        """
        seen = set()
        for attempt in range(DISCOVER_ATTEMPTS):
            run(['mdev', '-s'])
            found = {}
            suspects = {}
            probed = candidate_disks()
            for name in probed:
                try:
                    info = self.probe_disk(name)
                except (IOError, OSError, RuntimeError, ValueError) as error:
                    suspects[name] = str(error)
                    continue
                if info is not None:
                    found['/dev/' + name] = info
            members = set(os.path.basename(device) for device in found)
            for name in sorted(seen - members):
                # A member that was seen before and is gone now. decide()
                # stops the boot only when ONL lives on it.
                suspects[name] = 'IMSM member disappeared during discovery'
            holders = label_disks()
            # Keep waiting only while a member of the ONL array may still
            # be missing: fewer members than the metadata declares, or no
            # ONL partition visible on any disk yet. ONL partitions on
            # disks that are not members mean the array is not ours, so
            # there is nothing to wait for.
            complete = bool(members) and len(members) >= expected_members(found)
            foreign = bool(holders) and not (holders & members)
            settled = complete or foreign
            if settled or attempt == DISCOVER_ATTEMPTS - 1:
                engaged, reason = decide(members, suspects, holders)
                for name in sorted(suspects):
                    console('ONL mdraid: warning: %s: %s' % (name, suspects[name]))
                if not engaged:
                    if members or suspects:
                        console('ONL mdraid: standard ONL boot: ' + reason + '.')
                    else:
                        # The usual case on a platform without RAID: one line
                        # in the log, shown with the sysinit output, so that
                        # a boot log tells which path was taken and why.
                        print('ONL mdraid: standard ONL boot: %s (probed %s).' % (
                            reason, ', '.join(sorted(probed)) if probed else 'no candidate disk'))
                    return None
                print('ONL mdraid: RAID boot: ' + reason + '.')
                return found
            seen = members
            time.sleep(1)

    def select_array(self, found):
        """Require a complete container, then choose free MD device numbers."""
        count = expected_members(found)
        require(count >= 2 and len(found) == count,
                'Expected all %d IMSM members, found %d.' % (count, len(found)))
        levels = {}
        for device, info in sorted(found.items()):
            require(info.get('MD_LEVEL') == 'container' and info.get('MD_DEVICES') == str(count),
                    'Unsupported container: ' + device)
            current = identity(mdadm('--examine', '--scan', device))
            require(current['container_uuid'] == md_uuid(info['MD_UUID']), 'Container identity changed.')
            require(not self.state or self.state == current, 'Members describe different arrays.')
            self.state = current
            described = volume_levels(mdadm('--examine', device))
            require(not levels or levels == described, 'Members describe different volumes.')
            levels = described
        for volume in self.state['volumes']:
            require(volume['uuid'] in levels, 'Volume %s has no level description.' % volume['uuid'])
            volume.update(levels[volume['uuid']])
            require(2 <= volume['members'] <= count, 'Unexpected member count for volume ' + volume['uuid'])
            require(personality(volume['level']) in read('/proc/mdstat'),
                    'The kernel has no %s personality.' % volume['level'])
        numbers = {}
        for device in found:
            numbers.update(device_numbers(device))
        require(numbers, 'Cannot identify member devices.')
        no_holders(numbers.values())
        require(not any(entry[0] in numbers for entry in mounts()), 'A member device or partition is mounted.')
        for line in read('/proc/swaps').splitlines()[1:]:
            device = os.path.realpath(line.split()[0])
            if device.startswith('/dev/'):
                number = read(SYS_BLOCK + '/' + os.path.basename(device) + '/dev')
                require(number not in numbers, 'A member device is used for swap: ' + device)
        for path in ('/dev/md/onl-container', '/dev/md/onl-system'):
            require(not os.path.lexists(path), 'MD alias already exists: ' + path)
        needed = 1 + len(self.state['volumes'])
        free = ['md%d' % number for number in range(127, -1, -1)
                if not os.path.lexists(SYS_BLOCK + '/md%d' % number)
                and not os.path.lexists('/dev/md%d' % number)]
        require(len(free) >= needed,
                'Cannot find %d unused MD device numbers in the range 127 to 0.' % needed)
        for index, volume in enumerate(self.state['volumes']):
            volume['device'] = free[1 + index]
        self.volumes = self.state['volumes']
        self.state = dict(platform=self.platform, members=str(count), container=free[0],
                          container_uuid=self.state['container_uuid'])

    # -- state -----------------------------------------------------------

    def volumes_text(self):
        """Encode the volume list for the state file: device/member/level/uuid."""
        return ' '.join('%s/%s/%s/%s' % (v['device'], v['member'], v['level'], v['uuid'])
                        for v in self.volumes)

    def save_state(self):
        """Write the state that the later boot stages validate and use."""
        state = dict(self.state)
        state['volumes'] = self.volumes_text()
        atomic_write(STATE, ''.join('%s=%s\n' % item for item in sorted(state.items())))

    def load_state(self):
        """Validate the saved identity at every boot-stage boundary."""
        self.state = fields(read(STATE))
        require(self.state['platform'] == self.platform, 'RAID state belongs to another platform.')
        require(re.match(r'^md[0-9]+$', self.state['container']), 'Invalid MD state device.')
        require(block_device('/dev/' + self.state['container']), 'Missing MD container device.')
        require(re.match(r'^[0-9]+$', self.state['members']) and int(self.state['members']) >= 2,
                'Invalid member count.')
        md_uuid(self.state['container_uuid'])
        self.volumes = []
        for token in self.state['volumes'].split():
            parts = token.split('/')
            require(len(parts) == 4, 'Invalid volume state: ' + token)
            volume = dict(device=parts[0], member=parts[1], level=parts[2], uuid=md_uuid(parts[3]))
            require(re.match(r'^md[0-9]+$', volume['device']) and volume['device'] != self.state['container'],
                    'Invalid MD volume device: ' + token)
            require(block_device('/dev/' + volume['device']), 'Missing MD volume device: ' + volume['device'])
            require(re.match(r'^[0-9]+$', volume['member']), 'Invalid MD member.')
            require(volume['level'] in LEVELS.values(), 'Invalid MD level: ' + volume['level'])
            self.volumes.append(volume)
        require(self.volumes, 'No volume in the RAID state.')
        devices = [volume['device'] for volume in self.volumes]
        require(len(set(devices)) == len(devices), 'Duplicate volume device in the RAID state.')
        require(re.match(r'^md[0-9]+$', self.state['array']) and self.state['array'] in devices,
                'The system volume is not one of the volumes.')

    def system_volume(self):
        """Return the volume that carries the ONL partitions."""
        for volume in self.volumes:
            if volume['device'] == self.state['array']:
                return volume
        raise RuntimeError('The system volume is missing from the state.')

    # -- per-volume helpers ----------------------------------------------

    def md_value(self, device, key):
        """Read one live MD attribute of a volume."""
        return read(SYS_BLOCK + '/' + device + '/md/' + key)

    def set_md_value(self, device, key, value):
        """Write one live MD attribute of a volume."""
        with open(SYS_BLOCK + '/' + device + '/md/' + key, 'w') as stream:
            stream.write(value)

    def array_value(self, key):
        """Read one live attribute of the system volume."""
        return self.md_value(self.state['array'], key)

    def relation(self, volume):
        """Return the container relation as the kernel shows it, without the mark."""
        return self.state['container'] + '/' + volume['member']

    def check_volume(self, volume, readonly=False, idle=False):
        """Validate identity, level, membership and the container relationship of one volume."""
        device = volume['device']
        output = mdadm('--detail', '--export', '/dev/' + device)
        require(md_uuid(fields(output)['MD_UUID']) == volume['uuid'], 'Volume UUID does not match: ' + device)
        require(self.md_value(device, 'level') == volume['level'],
                'Expected a %s volume: %s' % (volume['level'], device))
        disks = self.md_value(device, 'raid_disks')
        require(re.match(r'^[0-9]+$', disks) and 2 <= int(disks) <= int(self.state['members']),
                'Unexpected raid_disks for %s: %s' % (device, disks))
        if volume['level'] in REDUNDANT:
            require(self.md_value(device, 'degraded') == '0', 'Degraded RAID boot is disabled: ' + device)
        relation = self.relation(volume)
        require(self.md_value(device, 'metadata_version') in ('external:/' + relation, 'external:-' + relation),
                'Unexpected MD container relationship: ' + device)
        if readonly:
            require(self.md_value(device, 'array_state') == 'readonly', 'Volume did not remain read-only: ' + device)
            require(self.md_value(device, 'metadata_version') == 'external:-' + relation,
                    'Missing read-only metadata flag: ' + device)
        if idle and volume['level'] in REDUNDANT:
            require(self.md_value(device, 'sync_action') == 'idle',
                    'RAID recovery or checking is in progress: ' + device)

    def check_array(self, readonly=False, idle=False):
        """Validate every volume of the container."""
        for volume in self.volumes:
            self.check_volume(volume, readonly=readonly, idle=idle)

    def mark_readonly(self, volume):
        """Mark a read-only volume so that mdmon keeps it read-only.

        When mdmon starts, it makes a read-only volume writable unless
        the metadata version starts with 'external:-'. mdadm 4.1 and
        4.2 set this mark for '--assemble --readonly'; mdadm 4.3 and
        later do not. The kernel always accepts this change for
        external metadata, and 'mdadm --readwrite' removes the mark.
        mdmon ignores RAID0 volumes; the mark is set there too, so that
        every volume is checked the same way.
        """
        device = volume['device']
        relation = self.relation(volume)
        if self.md_value(device, 'metadata_version') == 'external:/' + relation:
            require(self.md_value(device, 'array_state') == 'readonly', 'Volume did not remain read-only: ' + device)
            self.set_md_value(device, 'metadata_version', 'external:-' + relation)

    # -- mdmon -----------------------------------------------------------

    def monitor_alive(self):
        """Reject stale PIDs, zombies and processes unrelated to mdmon."""
        pid = int(read('/run/mdadm/' + self.state['container'] + '.pid'))
        require(pid > 1, 'Invalid mdmon PID.')
        os.kill(pid, 0)
        status = re.search(r'^State:\s+(\S+)', read('/proc/%d/status' % pid), re.M)
        require(status and status.group(1) not in ('Z', 'X'), 'mdmon has exited.')
        args = read('/proc/%d/cmdline' % pid).split('\0')
        require(any(os.path.basename(arg) in ('mdmon', '@mdmon', '@dmon') for arg in args),
                'PID does not belong to mdmon.')

    def start_monitor(self, takeover=False):
        """Start the static mdmon under the short name '@mdmon'.

        A later 'mdmon --takeover' reads only the first 99 bytes of the
        command line of the monitor it replaces and looks for 'mdmon' in
        the program name. The full tool path could be too long for that.

        By convention, a name that starts with '@' marks a storage
        daemon that must survive shutdown. Setting it is all that
        --offroot does, and mdmon 4.3 and later reject --offroot when
        the name already starts with '@'. Only systemd follows this
        convention; see protect_monitor() for sysvinit.

        mdmon manages the container, so it is started once whatever
        the levels of the volumes are; it does nothing for RAID0.
        """
        alias = RUN_DIR + '/@mdmon'
        if os.path.lexists(alias):
            os.unlink(alias)
        os.symlink(tool_path('mdmon'), alias)
        os.environ['PATH'] = RUN_DIR + ':' + os.environ.get('PATH', '/sbin:/usr/sbin:/bin:/usr/bin')
        run(['@mdmon'] + (['--takeover'] if takeover else []) +
            ['/dev/' + self.state['container']])
        self.monitor_alive()

    def protect_monitor(self):
        """Keep mdmon running when sysvinit ends all processes at shutdown.

        On SIGTERM mdmon marks the array clean and exits. Only mdmon
        can mark the array active again, so the kernel then blocks the
        next write forever and the reboot hangs. The link points to the
        PID file, so it stays correct when mdmon is started again.
        """
        mkdir(OMIT_DIR)
        link = OMIT_DIR + '/mdmon-' + self.state['container'] + '.pid'
        if os.path.lexists(link):
            os.unlink(link)
        os.symlink('/run/mdadm/' + self.state['container'] + '.pid', link)
        require(int(read(link)) > 1, 'Invalid mdmon PID in ' + link)

    def enable_writes(self):
        """Enable writes on every volume, with a live monitor and a writable MD state.

        For a redundant volume mdmon performs the transition after
        'mdadm --readwrite' signals it; for RAID0 mdadm sets the
        volume active itself.
        """
        self.monitor_alive()
        for volume in self.volumes:
            device = volume['device']
            mdadm('--readwrite', '/dev/' + device)
            for attempt in range(11):
                self.monitor_alive()
                if self.md_value(device, 'array_state') in ('read-auto', 'clean', 'active', 'active-idle'):
                    break
                require(attempt < 10, 'mdmon did not release the volume for writes: ' + device)
                time.sleep(1)

    # -- loader stage ----------------------------------------------------

    def prepare_run(self):
        """Prepare a separate tmpfs for /run without covering /tmp or live state."""
        if os.path.islink('/run'):
            require(os.path.realpath('/run') == '/tmp', 'Unexpected /run symlink target.')
            require(not os.path.lexists(RUN_DIR) and not os.path.lexists('/run/mdadm'),
                    'Existing RAID state prevents replacing the /run symlink.')
            os.unlink('/run')
        mkdir('/run')
        entries = mounts()
        current = [entry for entry in entries if entry[2] == '/run']
        require(len(current) <= 1, 'Stacked mounts on /run are not supported.')
        if current:
            require(current[0][3] == 'tmpfs', '/run must use tmpfs.')
        else:
            require(not os.listdir('/run') and
                    not any(entry[2].startswith('/run/') for entry in entries),
                    'Cannot mount tmpfs over existing /run contents or mounts.')
            run(['mount', '-t', 'tmpfs', '-o', 'mode=0755', 'tmpfs', '/run'])

    def raise_sync_speed(self, device):
        """Raise the resync speed limit of a volume and return the value that restores it.

        The result is None when the limit was left as it was.
        """
        try:
            current = self.md_value(device, 'sync_speed_max')
            match = re.match(r'^([0-9]+) \((local|system)\)$', current)
            if not match:
                print('ONL mdraid: unknown sync_speed_max format, limit not changed: ' + current)
                return None
            if int(match.group(1)) >= RESYNC_SPEED_MAX:
                return None
            self.set_md_value(device, 'sync_speed_max', str(RESYNC_SPEED_MAX))
        except (IOError, OSError, ValueError) as error:
            # A faster resync is optional. Keep waiting with the old limit.
            print('ONL mdraid: sync speed limit not changed: ' + str(error))
            return None
        console('ONL mdraid: %s: sync speed limit raised from %s to %d KiB/s for this wait.' %
                (device, current, RESYNC_SPEED_MAX))
        # A limit that follows the system default is restored by name.
        return 'system' if match.group(2) == 'system' else match.group(1)

    def restore_sync_speed(self, device, saved):
        """Put back the resync speed limit that raise_sync_speed() replaced."""
        if saved is None:
            return
        try:
            self.set_md_value(device, 'sync_speed_max', saved)
            current = self.md_value(device, 'sync_speed_max')
            if saved == 'system':
                restored = current.endswith(' (system)')
            else:
                restored = current == saved + ' (local)'
            require(restored, 'sync_speed_max is now ' + current)
            print('ONL mdraid: %s: sync speed limit restored: %s' % (device, current))
        except (IOError, OSError, RuntimeError) as error:
            # Report it, but never hide the result of the wait itself.
            console('ONL mdraid: %s: sync speed limit was not restored: %s' % (device, error))

    def wait_resync(self):
        """Wait for pending synchronization of every redundant volume.

        RAID0 volumes have nothing to synchronize and are skipped.
        """
        for volume in self.volumes:
            if volume['level'] not in REDUNDANT:
                continue
            self.speed_saved = None
            try:
                self.wait_sync_idle(volume)
            finally:
                # This runs on success and on failure, so the later boot stages
                # and the runtime system keep the original limit.
                self.restore_sync_speed(volume['device'], self.speed_saved)

    def wait_sync_idle(self, volume):
        """Poll one volume until no sync action is running or pending."""
        device = volume['device']
        progress = None
        changed = elapsed_seconds()
        reported = None
        while True:
            self.monitor_alive()
            require(self.md_value(device, 'degraded') == '0', 'RAID lost a member during resync: ' + device)
            action = self.md_value(device, 'sync_action')
            if action == 'idle' and self.md_value(device, 'resync_start') == 'none':
                self.check_volume(volume, idle=True)
                return
            current = self.md_value(device, 'sync_completed')
            now = elapsed_seconds()
            if current != progress:
                progress, changed = current, now
            require(now - changed < RESYNC_STALL,
                    'No resync progress update for %s seconds (%s, action=%s, completed=%s).' %
                    (RESYNC_STALL, device, action, current))
            if reported is None:
                # Nothing else uses the volume yet, so let the sync run faster.
                self.speed_saved = self.raise_sync_speed(device)
            if reported is None or now - reported >= 30:
                console('ONL mdraid: waiting for RAID %s on %s to finish: %s' % (action, device, current))
                reported = now
            time.sleep(5)

    def volume_labels(self, volume):
        """Return the approved labels found on the partitions of a volume."""
        devices = {}
        for path in partitions_of(volume['device']):
            device = '/dev/' + os.path.basename(path)
            require(block_device(device), 'Missing partition node: ' + device)
            rc, label = run(['blkid', '-p', '-s', 'LABEL', '-o', 'value', device], check=False)
            require(rc in (0, 2), 'Cannot read partition label: ' + device)
            if rc == 0 and label in LABELS:
                require(label not in devices, 'Duplicate ONL label on %s: %s' % (volume['device'], label))
                devices[label] = device
        return devices

    def find_system_volume(self):
        """Pick the volume that carries the ONL partitions and record its labels.

        The record is kept for diagnosis only. The label lookup of
        onl.blockdev finds the MD partitions on its own.
        """
        system = []
        for volume in self.volumes:
            labels = self.volume_labels(volume)
            if set(REQUIRED_LABELS).issubset(labels):
                system.append((volume, labels))
            else:
                require(not set(REQUIRED_LABELS) & set(labels),
                        'Only some ONL labels are on volume ' + volume['device'])
        require(len(system) == 1, 'Expected the ONL labels on exactly one volume, found %d.' % len(system))
        volume, labels = system[0]
        self.state['array'] = volume['device']
        atomic_write(DEVICE_MAP, json.dumps(labels, indent=4, sort_keys=True) + '\n')
        return volume

    def loader(self):
        """Assemble read-only, validate, then provide monitored RAID to ONL."""
        if raid_mode() in OFF_VALUES:
            console('ONL mdraid: disabled by onl_raid=off; standard ONL boot.')
            return
        if not tools_present():
            missing = [name for name in ('mdadm', 'mdmon') if not os.access(tool_path(name), os.X_OK)]
            if len(missing) == 1:
                # Half an installation is a packaging error worth a line on the console.
                console('ONL mdraid: %s is missing; RAID boot support is disabled.' % tool_path(missing[0]))
            else:
                # A loader built without onl-mdadm (not amd64): log only.
                print('ONL mdraid: standard ONL boot: no static mdadm/mdmon in this loader.')
            return
        found = self.discover()
        if not found:
            return
        self.prepare_run()
        self.select_array(found)
        conf = 'DEVICE %s\nHOMEHOST <ignore>\nAUTO -all\nDEVICE containers\n' % ' '.join(sorted(found))
        conf += 'ARRAY /dev/{container} metadata=imsm UUID={container_uuid}\n'.format(**self.state)
        for volume in self.volumes:
            conf += ('ARRAY /dev/%s container=%s member=%s UUID=%s\n' %
                     (volume['device'], self.state['container_uuid'], volume['member'], volume['uuid']))
        atomic_write(RUN_DIR + '/mdadm.conf', conf)
        print('ONL mdraid: selected /dev/%s (container), %s.' % (
            self.state['container'],
            ', '.join('/dev/%s (%s)' % (v['device'], v['level']) for v in self.volumes)))
        try:
            system = self.assemble()
        except Exception:
            # Leave nothing that a later program could mount or write to.
            self.teardown()
            raise
        print('ONL mdraid: RAID ready: /dev/%s (%s)' % (system['device'], system['level']))

    def assemble(self):
        """Assemble read-only, verify, start mdmon, enable writes and wait for resync."""
        mdadm('--assemble', '--scan', '--readonly', '--config=' + RUN_DIR + '/mdadm.conf')
        run(['mdev', '-s'])
        for volume in self.volumes:
            self.mark_readonly(volume)
        self.check_array(readonly=True)
        mkdir('/dev/md')
        os.symlink('/dev/' + self.state['container'], '/dev/md/onl-container')
        # The detail command opens the volumes and triggers partition discovery.
        run(['mdev', '-s'])
        system = self.find_system_volume()
        os.symlink('/dev/' + system['device'], '/dev/md/onl-system')
        self.start_monitor()
        self.enable_writes()
        self.wait_resync()
        self.save_state()
        return system

    def teardown(self):
        """Stop the arrays and mdmon after a failed loader stage.

        Best effort: every step is tried, and errors are reported but
        not raised, so that the original error reaches the log. With
        the arrays stopped, no partition of the RAID is visible and the
        member partitions stay removed, so nothing can be mounted by
        mistake. The configuration stays in /run/onl/mdadm.conf for
        assembling by hand from the loader shell.
        """
        console('ONL mdraid: stopping the arrays after the failure.')
        for path in (STATE, HANDOFF, DEVICE_MAP, '/dev/md/onl-system', '/dev/md/onl-container'):
            try:
                if os.path.lexists(path):
                    os.unlink(path)
            except OSError as error:
                console('ONL mdraid: cannot remove %s: %s' % (path, error))
        devices = [volume['device'] for volume in self.volumes] + [self.state.get('container')]
        for device in devices:
            if device and block_device('/dev/' + device):
                rc, output = run(tool_command('mdadm', ['--stop', '/dev/' + device]), check=False)
                if rc != 0:
                    console('ONL mdraid: cannot stop /dev/%s: %s' % (device, output))
        # mdmon leaves when its container is stopped; make sure of it.
        pidfile = '/run/mdadm/%s.pid' % self.state.get('container')
        try:
            pid = int(read(pidfile))
            for attempt in range(10):
                os.kill(pid, 0)
                if attempt == 0:
                    os.kill(pid, signal.SIGTERM)
                time.sleep(0.5)
            console('ONL mdraid: mdmon (PID %d) did not exit.' % pid)
        except (IOError, OSError, ValueError):
            pass
        console('ONL mdraid: arrays stopped; the configuration is kept in %s/mdadm.conf.' % RUN_DIR)

    # -- rootfs handoff --------------------------------------------------

    def array_mounts(self, require_root=False):
        """Reject unsupported, nested or stacked mounts before touching rootfs.

        Only the system volume may be mounted, and only at the ONL
        mount points; a mount of another volume stops the handoff.
        """
        system = device_numbers('/dev/' + self.state['array'])
        require(system, 'Cannot read MD device numbers.')
        others = {}
        for volume in self.volumes:
            if volume['device'] != self.state['array']:
                others.update(device_numbers('/dev/' + volume['device']))
        entries = mounts()
        targets = [entry[2] for entry in entries]
        allowed = (NEWROOT, '/mnt/onl/data', '/mnt/onl/images', '/mnt/onl/config', '/mnt/onl/boot', '/boot/efi')
        found = {}
        for number, root, target, fs in entries:
            require(number not in others, 'A volume other than the system volume is mounted: ' + target)
            if number in system:
                valid = (target in allowed and root == '/' and targets.count(target) == 1 and
                         not any(other.startswith(target + '/') for other in targets) and
                         (target != NEWROOT or fs == 'ext4'))
                require(valid, 'Unsupported MD rootfs layout or nested mount; handoff was not started.')
                found[target] = system[number]
        require(not require_root or NEWROOT in found, 'Cannot identify the rootfs device.')
        return found

    def check_new_rootfs(self):
        """Require a rootfs that carries the same RAID support as this loader.

        The runtime takeover runs the copies of these files in the
        rootfs, and onl-mounts in the rootfs needs the RAID-aware
        label lookup of onl.blockdev; both come from the same
        vendor-config-onl package as this module. A rootfs from a
        build without RAID support has no takeover hook: sendsigs
        would end mdmon at shutdown and the reboot would hang, so
        such a rootfs is refused before the handoff starts.
        """
        for path in (tool_path('mdadm'), tool_path('mdmon'), '/sbin/init',
                     '/usr/bin/python', '/usr/bin/onl-mdraid', '/etc/boot.d/05.mdraid'):
            require(os.access(NEWROOT + path, os.X_OK), 'The new rootfs is missing ' + path)
        for name in ('blockdev', 'mdraid'):
            require(glob.glob(NEWROOT + '/usr/lib/python*/*-packages/onl/%s/__init__.py' % name),
                    'The new rootfs is missing the onl.%s module.' % name)
        if os.path.isfile(NEWROOT + '/etc/onl/platform'):
            require(read(NEWROOT + '/etc/onl/platform') == self.platform, 'The new rootfs has a different platform.')
        atomic_write(NEWROOT + '/etc/onl/platform', self.platform + '\n')
        require(not os.path.islink(NEWROOT + '/run'), NEWROOT + '/run must not be a symlink.')
        mkdir(NEWROOT + '/run')

    def mounted_root(self):
        """Verify a rootfs that is not on the RAID and stays mounted during the handoff.

        An NFS rootfs is kept as the loader mounted it. Otherwise
        /newroot must be the overlay that 'swiprep --overlay' builds
        for BOOTMODE=SWI: the squashfs of the SWI kept in RAM as
        SWI_ROOTFS and mounted through a loop device on SWI_LOWER, a
        tmpfs on SWI_UPPER, and the overlay (or the old overlayfs)
        joining the two on /newroot.

        Every layer is checked before anything is unmounted. An
        overlay keeps its layers open without a visible mount, so a
        layer or a squashfs file on an MD volume would let every
        unmount succeed and 'mdadm --readonly' fail afterwards, with
        the ONL filesystems already unmounted. Returns the mountinfo
        description of /newroot and its layers; resume-root compares
        it with the live mounts after the process cleanup.
        """
        root = mount_info(NEWROOT)
        if root['fs'] in ('nfs', 'nfs4'):
            return root
        require(root['fs'] in ('overlay', 'overlayfs'),
                'Unsupported RAID rootfs type: %s. Expected ONL-DATA ext4, standard SWI overlay or NFS.' % root['fs'])
        lower = mount_info(SWI_LOWER)
        upper = mount_info(SWI_UPPER)
        options = root['options']
        expected = dict(lowerdir=SWI_LOWER, upperdir=SWI_UPPER)
        if root['fs'] == 'overlay':
            expected.update(upperdir=SWI_UPPER + '/upper', workdir=SWI_UPPER + '/work')
        require(root['root'] == '/' and
                all(mount_path(options.get(key, '')) == value for key, value in expected.items()),
                'Unsupported SWI overlay paths.')
        require(lower['fs'] == 'squashfs' and lower['root'] == '/' and
                upper['fs'] == 'tmpfs' and upper['root'] == '/',
                'SWI requires a squashfs lower layer and a tmpfs upper layer.')
        backing = loop_backing_file(lower['number'])
        require(backing == SWI_ROOTFS, 'Unexpected SWI squashfs backing file: ' + backing)
        try:
            info = os.stat(backing)
        except OSError as error:
            raise RuntimeError('Cannot stat SWI squashfs backing file %s: %s' % (backing, error))
        number = '%d:%d' % (os.major(info.st_dev), os.minor(info.st_dev))
        # The loader keeps /tmp on tmpfs, or on the initramfs itself.
        backing_fs = sorted(set(entry[3] for entry in mounts() if entry[0] == number))
        require(stat.S_ISREG(info.st_mode) and any(fs in RAM_FILESYSTEMS for fs in backing_fs),
                'The SWI squashfs backing file must be a regular file in RAM, outside MD '
                '(device=%s, filesystem=%s).' % (number, ','.join(backing_fs) or 'unknown'))
        for target in (SWI_LOWER, SWI_UPPER):
            require(not any(entry[2].startswith(target + '/') for entry in mounts()),
                    'Nested mount below an SWI layer: ' + target)
        root.update(lower=lower, upper=upper)
        return root

    def prepare_root(self):
        """Quiesce RAID while mdmon is alive, before BusyBox init kills processes.

        Nothing is unmounted before every check has passed, so a
        failure here leaves the boot exactly as /bin/boot built it.
        """
        if not os.path.isfile(STATE):
            return
        self.load_state()
        self.monitor_alive()
        self.check_new_rootfs()
        require(not os.path.lexists(HANDOFF), 'A rootfs handoff is already prepared.')
        self.check_array(idle=True)
        require(not any(entry[2].startswith(NEWROOT + '/') for entry in mounts()),
                'Nested mounts below %s are not supported before handoff.' % NEWROOT)
        current = self.array_mounts()
        if NEWROOT in current:
            root = current[NEWROOT]
            require(run(['blkid', '-p', '-s', 'LABEL', '-o', 'value', root])[1] == 'ONL-DATA',
                    'The rootfs is not ONL-DATA.')
            handoff = dict(mode='md', device=root)
        else:
            handoff = dict(mode='mounted', mount=self.mounted_root())
        # Serialize now, so that a record that cannot be written stops
        # the stage before any filesystem is unmounted.
        record = json.dumps(handoff, sort_keys=True) + '\n'
        print('ONL mdraid: rootfs handoff mode: ' + handoff['mode'])
        require(len(read('/proc/swaps').splitlines()) <= 1, 'Disable swap before rootfs handoff.')
        for volume in self.volumes:
            no_holders(device_numbers('/dev/' + volume['device']).values())
        atomic_write(RUN_DIR + '/mdraid.mounts',
                     ''.join('%s %s\n' % (device, target) for target, device in sorted(current.items())))
        print('ONL mdraid: unmounting filesystems before init process cleanup.')
        for target in sorted(current):
            self.monitor_alive()
            run(['umount', target])
        require(not self.array_mounts(), 'An MD filesystem is still mounted.')
        for volume in self.volumes:
            # --wait-clean returns at once for RAID0.
            mdadm('--wait-clean', '/dev/' + volume['device'])
            self.monitor_alive()
            mdadm('--readonly', '/dev/' + volume['device'])
        self.check_array(readonly=True, idle=True)
        atomic_write(HANDOFF, record)
        print('ONL mdraid: rootfs handoff prepared; MD is read-only and unmounted.')

    def resume_root(self):
        """Restore monitored rootfs after PID 1 has completed process cleanup.

        The stage can run again after a failure (the switchroot script
        retries it from a recovery shell), so a rootfs or a /run that
        an earlier attempt already mounted is verified, not mounted
        twice.
        """
        handoff = json.loads(read(HANDOFF))
        require(isinstance(handoff, dict) and handoff.get('mode') in ('md', 'mounted'),
                'Invalid rootfs handoff mode.')
        self.load_state()
        self.check_array(idle=True)
        if handoff['mode'] == 'md':
            root = str(handoff['device'])
            require(root in device_numbers('/dev/' + self.state['array']).values(),
                    'The handoff root device is outside the system volume.')
            require(run(['blkid', '-p', '-s', 'LABEL', '-o', 'value', root])[1] == 'ONL-DATA',
                    'Handoff root label changed.')
        else:
            require(self.mounted_root() == handoff['mount'], 'The prepared rootfs mount changed.')
            require(not self.array_mounts(), 'An MD filesystem is still mounted.')
        try:
            self.monitor_alive()
        except (IOError, OSError, ValueError, RuntimeError):
            require(not self.array_mounts(), 'An MD filesystem is still mounted.')
            self.check_array(readonly=True)
            self.start_monitor(takeover=True)
        self.enable_writes()
        if handoff['mode'] == 'md':
            if not any(entry[2] == NEWROOT for entry in mounts()):
                run(['mount', '-t', 'ext4', '-o', 'rw', root, NEWROOT])
            require(self.array_mounts(require_root=True)[NEWROOT] == root,
                    'Mounted rootfs does not match the prepared device.')
        require(os.access(NEWROOT + '/sbin/init', os.X_OK), 'The remounted rootfs has no init.')
        require(read(NEWROOT + '/etc/onl/platform') == self.platform, 'Remounted rootfs platform changed.')
        if any(entry[2] == NEWROOT + '/run' for entry in mounts()):
            source, target = mount_info('/run'), mount_info(NEWROOT + '/run')
            require(all(source[key] == target[key] for key in ('number', 'root', 'fs')),
                    NEWROOT + '/run does not match the loader /run.')
        else:
            run(['mount', '--bind', '/run', NEWROOT + '/run'])
        self.monitor_alive()
        print('ONL mdraid: mdmon restarted and rootfs restored.')

    def takeover(self):
        """Validate the runtime array, then replace the loader's mdmon.

        The new monitor runs from the rootfs copy of the static tools,
        so the loader's files are no longer in use afterwards.
        """
        self.load_state()
        self.check_array()
        require(os.path.isfile(DEVICE_MAP), 'Runtime MD device map is missing.')
        self.start_monitor(takeover=True)
        self.protect_monitor()
        print('ONL mdraid: mdmon takeover completed for /dev/' + self.state['container'])


STAGES = ('loader', 'prepare-root', 'resume-root', 'takeover')
USAGE = 'Usage: onl-mdraid loader | prepare-root | resume-root | takeover | tool {mdadm|mdmon} [ARGS...]'


def dispatch(args):
    """Run one boot stage, or exec a static tool with the stage environment."""
    if len(args) >= 2 and args[0] == 'tool':
        command = tool_command(args[1], args[2:])
        os.environ.update(LC_ALL='C', MDADM_NO_MDMON='1', MDADM_NO_SYSTEMCTL='1')
        os.execv(command[0], command)
    require(os.geteuid() == 0, 'Run this command as root.')
    require(len(args) == 1 and args[0] in STAGES, USAGE)
    getattr(Raid(), args[0].replace('-', '_'))()


def main(argv=None):
    """Command line entry point; the exit status reports success or failure."""
    if argv is None:
        argv = sys.argv[1:]
    try:
        dispatch(argv)
    except KeyError as error:
        sys.exit('ONL mdraid: Missing required field: ' + str(error))
    except (IOError, OSError, ValueError, RuntimeError) as error:
        sys.exit('ONL mdraid: ' + str(error))


if __name__ == '__main__':
    main()
