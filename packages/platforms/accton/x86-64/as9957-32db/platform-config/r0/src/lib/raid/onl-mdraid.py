#!/usr/bin/env python
"""Boot an existing two-disk IMSM RAID1 with ONL's Python 2.7 loader."""

from __future__ import print_function

import glob
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.realpath(__file__))
# ONL adds this platform directory to PATH on the running system.
TOOLS = os.path.dirname(HERE) + '/sbin'
SYS_BLOCK = '/sys/class/block'
RUN_DIR = '/run/onl'
STATE = RUN_DIR + '/mdraid.state'
DEVICE_MAP = RUN_DIR + '/block-devices.json'
HANDOFF = RUN_DIR + '/mdraid.handoff'
# The sysvinit 'sendsigs' script does not signal the PIDs listed here.
OMIT_DIR = '/run/sendsigs.omit.d'
REQUIRED_LABELS = ('ONL-BOOT', 'ONL-CONFIG', 'ONL-IMAGES', 'ONL-DATA')
LABELS = REQUIRED_LABELS + ('EFI System', 'ONIE-BOOT', 'ACC-DIAG')
# Allow 15 minutes without a visible resync progress update.
RESYNC_STALL = 900
# Resync speed limit used only while the loader waits, in KiB/s.
RESYNC_SPEED_MAX = 5000000


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


def tool_command(name, args):
    """Build a command for a static tool shipped with this platform.

    The loader and the runtime system both use these tools, so no boot
    stage depends on the mdadm version of the Debian release.
    """
    require(name in ('mdadm', 'mdmon'), 'Expected mdadm or mdmon.')
    return [TOOLS + '/' + name] + list(args)


def mdadm(*args):
    """Run the platform mdadm without automatic monitor or systemctl startup."""
    return run(tool_command('mdadm', args))[1]


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


def device_numbers(device):
    """Return a whole device and its partitions, indexed by major:minor."""
    base = SYS_BLOCK + '/' + os.path.basename(device)
    paths = [base] + sorted(glob.glob(base + '/' + os.path.basename(device) + 'p*'))
    return {read(path + '/dev'): '/dev/' + os.path.basename(path)
            for path in paths if os.path.isfile(path + '/dev')}


def no_holders(devices):
    """Reject members or MD partitions owned by another block device."""
    for device in devices:
        require(not glob.glob(SYS_BLOCK + '/' + os.path.basename(device) + '/holders/*'),
                'Device has a holder: ' + device)


def identity(text):
    """Select one IMSM container and one volume without using stored names."""
    arrays = [fields('\n'.join(line.split()[1:])) for line in text.splitlines()
              if line.split()[:1] == ['ARRAY']]
    containers = [entry for entry in arrays if entry.get('metadata') == 'imsm']
    volumes = [entry for entry in arrays if 'container' in entry]
    require(len(arrays) == 2 and len(containers) == 1 and len(volumes) == 1,
            'Expected one IMSM container and one volume.')
    container, volume = containers[0], volumes[0]
    container_uuid = md_uuid(container['UUID'])
    require(md_uuid(volume['container']) == container_uuid and
            re.match(r'^[0-9]+$', volume['member']), 'Invalid IMSM container relationship.')
    return dict(container_uuid=container_uuid, volume_uuid=md_uuid(volume['UUID']), member=volume['member'])


class Raid(object):
    """Keep array identity local while executing the four ONL boot stages."""

    def __init__(self):
        """Read the platform selected by ONL, without a hard-coded model name."""
        self.platform = read('/etc/onl/platform')
        require(re.match(r'^[A-Za-z0-9][A-Za-z0-9_.-]*$', self.platform), 'Invalid ONL platform.')
        self.state = {}

    def discover(self):
        """Wait for members and distinguish clean non-RAID disks from probe errors."""
        seen = set()
        for attempt in range(11):
            run(['mdev', '-s'])
            found = {}
            for path in sorted(glob.glob(SYS_BLOCK + '/nvme*n*')):
                name = os.path.basename(path)
                if not re.match(r'^nvme[0-9]+n[0-9]+$', name):
                    continue
                device = '/dev/' + name
                require(block_device(device), 'Not a block device: ' + device)
                sectors = int(read(path + '/size'))
                require(sectors > 0, 'Empty device: ' + device)
                with open(device, 'rb') as stream:
                    for offset in (0, (sectors - 1) * 512):
                        stream.seek(offset)
                        require(len(stream.read(512)) == 512, 'Short read: ' + device)
                rc, output = run(tool_command('mdadm', ['--examine', '--export', device]), check=False)
                if rc == 0:
                    info = fields(output)
                    require(info.get('MD_METADATA') == 'imsm', 'Unsupported RAID metadata: ' + device)
                    found[device] = info
                elif rc == 1:
                    status, signature = run(['blkid', '-p', '-o', 'export', device], check=False)
                    require(status in (0, 2), 'Cannot identify signatures: ' + device)
                    require('raid' not in fields(signature).get('TYPE', '').lower(),
                            'mdadm cannot read the RAID signature: %s\n%s' % (device, output))
                else:
                    raise RuntimeError('mdadm examination failed (%s): %s\n%s' % (rc, device, output))
            require(seen.issubset(found), 'An IMSM member disappeared.')
            if len(found) >= 2 or attempt == 10:
                return found
            seen = set(found)
            time.sleep(1)

    def select_array(self, found):
        """Require two matching members before selecting free MD device numbers."""
        require(len(found) == 2, 'Expected exactly two IMSM members.')
        for device, info in sorted(found.items()):
            require(info.get('MD_LEVEL') == 'container' and info.get('MD_DEVICES') == '2',
                    'Unsupported container: ' + device)
            current = identity(mdadm('--examine', '--scan', device))
            require(current['container_uuid'] == md_uuid(info['MD_UUID']), 'Container identity changed.')
            require(not self.state or self.state == current, 'Members describe different arrays.')
            self.state = current
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
        for path in ('/lib/boot-custom', '/dev/md/onl-container', '/dev/md/onl-system'):
            require(not os.path.lexists(path), 'MD alias or boot hook already exists: ' + path)
        free = ['md%d' % number for number in range(127, -1, -1)
                if not os.path.lexists(SYS_BLOCK + '/md%d' % number)
                and not os.path.lexists('/dev/md%d' % number)]
        require(len(free) >= 2, 'Cannot find two unused MD device numbers in the range 127 to 0.')
        self.state.update(platform=self.platform, container=free[0], array=free[1])

    def load_state(self):
        """Validate the saved identity at every boot-stage boundary."""
        self.state = fields(read(STATE))
        require(self.state['platform'] == self.platform, 'RAID state belongs to another platform.')
        for key in ('container', 'array'):
            require(re.match(r'^md[0-9]+$', self.state[key]), 'Invalid MD state device.')
            require(block_device('/dev/' + self.state[key]), 'Missing MD state device.')
        require(self.state['container'] != self.state['array'], 'Container and volume must differ.')
        require(re.match(r'^[0-9]+$', self.state['member']), 'Invalid MD member.')
        md_uuid(self.state['volume_uuid'])

    def array_value(self, key):
        """Read one live volume attribute."""
        return read(SYS_BLOCK + '/' + self.state['array'] + '/md/' + key)

    def check_array(self, readonly=False, idle=False):
        """Validate volume identity, membership and the current container relationship."""
        device = '/dev/' + self.state['array']
        output = mdadm('--detail', '--export', device)
        require(md_uuid(fields(output)['MD_UUID']) == self.state['volume_uuid'], 'Volume UUID does not match.')
        require(self.array_value('level') == 'raid1' and self.array_value('raid_disks') == '2',
                'Expected a two-disk RAID1 volume.')
        require(self.array_value('degraded') == '0', 'Degraded RAID1 boot is disabled.')
        relation = self.state['container'] + '/' + self.state['member']
        require(self.array_value('metadata_version') in ('external:/' + relation, 'external:-' + relation),
                'Unexpected MD container relationship.')
        if readonly:
            require(self.array_value('array_state') == 'readonly', 'Volume did not remain read-only.')
            require(self.array_value('metadata_version') == 'external:-' + relation,
                    'Missing read-only metadata flag.')
        if idle:
            require(self.array_value('sync_action') == 'idle', 'RAID recovery or checking is in progress.')

    def mark_readonly(self):
        """Mark the read-only volume so that mdmon keeps it read-only.

        When mdmon starts, it makes a read-only volume writable unless
        the metadata version starts with 'external:-'. mdadm 4.1 and
        4.2 set this mark for '--assemble --readonly'; mdadm 4.3 and
        later do not. The kernel always accepts this change for
        external metadata, and 'mdadm --readwrite' removes the mark.
        """
        relation = self.state['container'] + '/' + self.state['member']
        if self.array_value('metadata_version') == 'external:/' + relation:
            require(self.array_value('array_state') == 'readonly', 'Volume did not remain read-only.')
            self.set_array_value('metadata_version', 'external:-' + relation)

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
        """Start the platform mdmon under the short name '@mdmon'.

        A later 'mdmon --takeover' reads only the first 99 bytes of the
        command line of the monitor it replaces and looks for 'mdmon' in
        the program name. The full tool path could be too long for that.

        By convention, a name that starts with '@' marks a storage
        daemon that must survive shutdown. Setting it is all that
        --offroot does, and mdmon 4.3 and later reject --offroot when
        the name already starts with '@'. Only systemd follows this
        convention; see protect_monitor() for sysvinit.
        """
        alias = RUN_DIR + '/@mdmon'
        if os.path.lexists(alias):
            os.unlink(alias)
        os.symlink(tool_command('mdmon', [])[0], alias)
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
        """Enable writes only with a live monitor and a writable MD state."""
        self.monitor_alive()
        mdadm('--readwrite', '/dev/' + self.state['array'])
        for attempt in range(11):
            self.monitor_alive()
            if self.array_value('array_state') in ('read-auto', 'clean', 'active', 'active-idle'):
                return
            require(attempt < 10, 'mdmon did not release the array for writes.')
            time.sleep(1)

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

    def set_array_value(self, key, value):
        """Write one live volume attribute."""
        with open(SYS_BLOCK + '/' + self.state['array'] + '/md/' + key, 'w') as stream:
            stream.write(value)

    def raise_sync_speed(self):
        """Raise the resync speed limit and return the value that restores it.

        The result is None when the limit was left as it was.
        """
        try:
            current = self.array_value('sync_speed_max')
            match = re.match(r'^([0-9]+) \((local|system)\)$', current)
            if not match:
                print('ONL mdraid: unknown sync_speed_max format, limit not changed: ' + current)
                return None
            if int(match.group(1)) >= RESYNC_SPEED_MAX:
                return None
            self.set_array_value('sync_speed_max', str(RESYNC_SPEED_MAX))
        except (IOError, OSError, ValueError) as error:
            # A faster resync is optional. Keep waiting with the old limit.
            print('ONL mdraid: sync speed limit not changed: ' + str(error))
            return None
        console('ONL mdraid: sync speed limit raised from %s to %d KiB/s for this wait.' %
                (current, RESYNC_SPEED_MAX))
        # A limit that follows the system default is restored by name.
        return 'system' if match.group(2) == 'system' else match.group(1)

    def restore_sync_speed(self, saved):
        """Put back the resync speed limit that raise_sync_speed() replaced."""
        if saved is None:
            return
        try:
            self.set_array_value('sync_speed_max', saved)
            current = self.array_value('sync_speed_max')
            if saved == 'system':
                restored = current.endswith(' (system)')
            else:
                restored = current == saved + ' (local)'
            require(restored, 'sync_speed_max is now ' + current)
            print('ONL mdraid: sync speed limit restored: ' + current)
        except (IOError, OSError, RuntimeError) as error:
            # Report it, but never hide the result of the wait itself.
            console('ONL mdraid: sync speed limit was not restored: ' + str(error))

    def wait_resync(self):
        """Wait for pending synchronization before ONL mounts the volume."""
        self.speed_saved = None
        try:
            self.wait_sync_idle()
        finally:
            # This runs on success and on failure, so the later boot stages
            # and the runtime system keep the original limit.
            self.restore_sync_speed(self.speed_saved)

    def wait_sync_idle(self):
        """Poll the volume until no sync action is running or pending."""
        progress = None
        changed = elapsed_seconds()
        reported = None
        while True:
            self.monitor_alive()
            require(self.array_value('degraded') == '0', 'RAID1 lost a member during resync.')
            action = self.array_value('sync_action')
            if action == 'idle' and self.array_value('resync_start') == 'none':
                self.check_array(idle=True)
                return
            current = self.array_value('sync_completed')
            now = elapsed_seconds()
            if current != progress:
                progress, changed = current, now
            require(now - changed < RESYNC_STALL,
                    'No resync progress update for %s seconds (action=%s, completed=%s).' %
                    (RESYNC_STALL, action, current))
            if reported is None:
                # Nothing else uses the volume yet, so let the sync run faster.
                self.speed_saved = self.raise_sync_speed()
            if reported is None or now - reported >= 30:
                console('ONL mdraid: waiting for RAID %s to finish: %s' % (action, current))
                reported = now
            time.sleep(5)

    def partition_map(self):
        """Publish only approved labels found on the verified MD volume."""
        base = SYS_BLOCK + '/' + self.state['array']
        devices = {}
        for path in sorted(glob.glob(base + '/' + self.state['array'] + 'p*')):
            if not os.path.isfile(path + '/partition'):
                continue
            device = '/dev/' + os.path.basename(path)
            require(block_device(device), 'Missing partition node: ' + device)
            rc, label = run(['blkid', '-p', '-s', 'LABEL', '-o', 'value', device], check=False)
            require(rc in (0, 2), 'Cannot read partition label: ' + device)
            if rc == 0 and label in LABELS:
                require(label not in devices, 'Duplicate ONL label: ' + label)
                devices[label] = device
        require(set(REQUIRED_LABELS).issubset(devices), 'Duplicate or missing ONL labels on the MD volume.')
        atomic_write(DEVICE_MAP, json.dumps(devices, indent=4, sort_keys=True) + '\n')

    def loader(self):
        """Assemble read-only, validate, then provide monitored RAID to ONL."""
        found = self.discover()
        if not found:
            print('ONL mdraid: no IMSM RAID found; using standard ONL mounts.')
            return
        self.prepare_run()
        require('[raid1]' in read('/proc/mdstat'), 'The kernel has no RAID1 personality.')
        self.select_array(found)
        conf = 'DEVICE %s\nHOMEHOST <ignore>\nAUTO -all\nDEVICE containers\n' % ' '.join(sorted(found))
        conf += ('ARRAY /dev/{container} metadata=imsm UUID={container_uuid}\n'
                 'ARRAY /dev/{array} container={container_uuid} '
                 'member={member} UUID={volume_uuid}\n').format(**self.state)
        atomic_write(RUN_DIR + '/mdadm.conf', conf)
        print('ONL mdraid: selected /dev/{container} (container), /dev/{array} (volume).'.format(**self.state))
        mdadm('--assemble', '--scan', '--readonly', '--config=' + RUN_DIR + '/mdadm.conf')
        run(['mdev', '-s'])
        self.mark_readonly()
        self.check_array(readonly=True)
        mkdir('/dev/md')
        for role, key in (('container', 'container'), ('system', 'array')):
            os.symlink('/dev/' + self.state[key], '/dev/md/onl-' + role)
        # The detail command opens the volume and triggers partition discovery.
        run(['mdev', '-s'])
        self.partition_map()
        self.start_monitor()
        self.enable_writes()
        self.wait_resync()
        atomic_write(STATE, ''.join('%s=%s\n' % item for item in sorted(self.state.items())))
        os.symlink(HERE + '/boot-custom', '/lib/boot-custom')
        print('ONL mdraid: RAID ready: /dev/' + self.state['array'])

    def array_mounts(self, require_root=False):
        """Reject unsupported, nested or stacked mounts before touching rootfs."""
        numbers = device_numbers('/dev/' + self.state['array'])
        require(numbers, 'Cannot read MD device numbers.')
        entries = mounts()
        targets = [entry[2] for entry in entries]
        allowed = ('/newroot', '/mnt/onl/data', '/mnt/onl/images', '/mnt/onl/config', '/mnt/onl/boot', '/boot/efi')
        found = {}
        for number, root, target, fs in entries:
            if number in numbers:
                valid = (target in allowed and root == '/' and targets.count(target) == 1 and
                         not any(other.startswith(target + '/') for other in targets) and
                         (target != '/newroot' or fs == 'ext4'))
                require(valid, 'Unsupported MD rootfs layout or nested mount; handoff was not started.')
                found[target] = numbers[number]
        require(not require_root or '/newroot' in found, 'Cannot identify the rootfs device.')
        return found

    def prepare_root(self):
        """Quiesce RAID while mdmon is alive, before BusyBox init kills processes."""
        if not os.path.isfile(STATE):
            return
        self.load_state()
        self.monitor_alive()
        require('def resolve_block_device(' in read('/newroot/usr/lib/python2.7/dist-packages/onl/mounts/__init__.py'),
                'The new rootfs is missing the MD device-map patch.')
        # The runtime takeover uses the copy of these files in the rootfs.
        onl = '/lib/platform-config/' + self.platform + '/onl'
        for path in (onl + '/sbin/mdadm', onl + '/sbin/mdmon',
                     '/sbin/init', '/usr/bin/python', '/etc/boot.d/05.mdraid'):
            require(os.access('/newroot' + path, os.X_OK), 'The new rootfs is missing ' + path)
        require(os.path.isfile('/newroot' + onl + '/raid/onl-mdraid.py'),
                'The new rootfs is missing the RAID helper.')
        if os.path.isfile('/newroot/etc/onl/platform'):
            require(read('/newroot/etc/onl/platform') == self.platform, 'The new rootfs has a different platform.')
        atomic_write('/newroot/etc/onl/platform', self.platform + '\n')
        require(not os.path.islink('/newroot/run'), '/newroot/run must not be a symlink.')
        mkdir('/newroot/run')
        require(not os.path.lexists(HANDOFF) and not os.path.lexists('/bin/switchroot.mdraid-original'),
                'A rootfs handoff is already prepared.')
        require(os.path.isfile('/bin/switchroot') and os.access('/bin/switchroot', os.X_OK),
                'Missing native switchroot script.')
        actions = [line.split(':', 3) for line in read('/etc/inittab').splitlines() if not line.startswith('#')]
        require([entry[3] for entry in actions if len(entry) == 4 and entry[2] == 'restart'] == ['/bin/switchroot'],
                'Unexpected init restart action.')
        self.check_array(idle=True)
        current = self.array_mounts(require_root=True)
        root = current['/newroot']
        require(run(['blkid', '-p', '-s', 'LABEL', '-o', 'value', root])[1] == 'ONL-DATA',
                'The rootfs is not ONL-DATA.')
        require(len(read('/proc/swaps').splitlines()) <= 1, 'Disable swap before rootfs handoff.')
        no_holders(device_numbers('/dev/' + self.state['array']).values())
        atomic_write(RUN_DIR + '/mdraid.mounts',
                     ''.join('%s %s\n' % (device, target) for target, device in sorted(current.items())))
        shutil.copy2('/bin/switchroot', '/bin/switchroot.mdraid-original')
        shutil.copy2(HERE + '/switchroot', '/bin/switchroot.mdraid-new')
        os.chmod('/bin/switchroot.mdraid-new', 0o755)
        print('ONL mdraid: unmounting filesystems before init process cleanup.')
        for target in sorted(current):
            self.monitor_alive()
            run(['umount', target])
        require(not self.array_mounts(), 'An MD filesystem is still mounted.')
        mdadm('--wait-clean', '/dev/' + self.state['array'])
        self.monitor_alive()
        mdadm('--readonly', '/dev/' + self.state['array'])
        self.check_array(readonly=True, idle=True)
        os.rename('/bin/switchroot.mdraid-new', '/bin/switchroot')
        atomic_write(HANDOFF, 'root_device=%s\n' % root)
        print('ONL mdraid: rootfs handoff prepared; MD is read-only and unmounted.')

    def resume_root(self):
        """Restore monitored rootfs after PID 1 has completed process cleanup."""
        root = fields(read(HANDOFF))['root_device']
        self.load_state()
        self.check_array(idle=True)
        require(root in device_numbers('/dev/' + self.state['array']).values(),
                'The handoff root device is outside this MD array.')
        require(run(['blkid', '-p', '-s', 'LABEL', '-o', 'value', root])[1] == 'ONL-DATA',
                'Handoff root label changed.')
        try:
            self.monitor_alive()
        except (IOError, OSError, ValueError, RuntimeError):
            require(not self.array_mounts(), 'An MD filesystem is still mounted.')
            self.check_array(readonly=True)
            self.start_monitor(takeover=True)
        self.enable_writes()
        if not any(entry[2] == '/newroot' for entry in mounts()):
            run(['mount', '-t', 'ext4', '-o', 'rw', root, '/newroot'])
        require(self.array_mounts(require_root=True)['/newroot'] == root,
                'Mounted rootfs does not match the prepared device.')
        require(os.access('/newroot/sbin/init', os.X_OK), 'The remounted rootfs has no init.')
        require(read('/newroot/etc/onl/platform') == self.platform, 'Remounted rootfs platform changed.')
        run(['mount', '--bind', '/run', '/newroot/run'])
        self.monitor_alive()
        print('ONL mdraid: mdmon restarted and rootfs restored.')

    def takeover(self):
        """Validate the runtime array, then replace the loader's mdmon.

        The new monitor runs from the rootfs copy of the platform tools,
        so the loader's files are no longer in use afterwards.
        """
        self.load_state()
        self.check_array()
        require(os.path.isfile(DEVICE_MAP), 'Runtime MD device map is missing.')
        self.start_monitor(takeover=True)
        self.protect_monitor()
        print('ONL mdraid: mdmon takeover completed for /dev/' + self.state['container'])


def main():
    """Dispatch boot stages while preserving direct-tool output and exit status."""
    args = sys.argv[1:]
    if len(args) >= 2 and args[0] == 'tool':
        command = tool_command(args[1], args[2:])
        os.environ.update(LC_ALL='C', MDADM_NO_MDMON='1', MDADM_NO_SYSTEMCTL='1')
        os.execv(command[0], command)
    require(os.geteuid() == 0, 'Run this command as root.')
    require(len(args) == 1 and args[0] in ('loader', 'prepare-root', 'resume-root', 'takeover'),
            'Usage: onl-mdraid.py loader | prepare-root | resume-root | takeover | tool {mdadm|mdmon} [ARGS...]')
    getattr(Raid(), args[0].replace('-', '_'))()


if __name__ == '__main__':
    try:
        main()
    except KeyError as error:
        sys.exit('ONL mdraid: Missing required field: ' + str(error))
    except (IOError, OSError, ValueError, RuntimeError) as error:
        sys.exit('ONL mdraid: ' + str(error))
