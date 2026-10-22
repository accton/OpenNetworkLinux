"""Block device lookup that understands RAID member disks.

ONL finds its partitions by file system label. On a mirrored RAID
(for example Intel IMSM RAID1) the same label is visible on every
member disk and on the MD device, so a plain "blkid -L" may answer
with a member partition. Writing to a member partition breaks the
mirror. This module is the single place that turns a label into a
device, and it is shared by onl.mounts and by the blkid parser of
onl.install.

Rules, applied in this order:

1. A device that lives on an MD device (md126p4, md0, ...) wins.
2. A device that is a RAID member, or whose whole disk is a RAID
   member, is refused. A member is either listed under
   /sys/block/md*/slaves (the array is running) or carries a RAID
   superblock (the array is not assembled, or assembly failed).
3. Any other device is used, exactly as before.

A platform without RAID never reaches rule 1 or rule 2, so its
behaviour does not change.

The module works with the util-linux blkid of the ONL loader
(2.20) and of the Debian rootfs. It runs under Python 2.7 and 3.
"""
import os
import subprocess

SYS_BLOCK = '/sys/block'
SYS_CLASS_BLOCK = '/sys/class/block'
DEV_DIR = '/dev'
BLKID = 'blkid'

# Signature at the start of the Intel IMSM anchor block, which sits
# two sectors before the end of a member disk (see mdadm super-intel.c).
IMSM_SIGNATURE = b'Intel Raid ISM Cfg Sig. '
SECTOR_SIZES = (512, 4096)


class RaidMemberError(LookupError):
    """The label exists only on RAID member disks."""


def _run(args):
    """Run a command and return its output text, or None when it fails."""
    try:
        with open(os.devnull, 'w') as null:
            output = subprocess.check_output(args, stderr=null)
    except (OSError, subprocess.CalledProcessError):
        return None
    if not isinstance(output, str):
        output = output.decode('utf-8', 'replace')
    return output


def _read_text(path):
    """Return the stripped content of a small sysfs file, or None."""
    try:
        with open(path) as handle:
            return handle.read().strip()
    except (IOError, OSError):
        return None


def device_name(device):
    """Return the kernel name of a device node, e.g. nvme0n1p4."""
    if not device:
        return None
    return os.path.basename(os.path.realpath(device))


def whole_disk(device):
    """Return the whole-disk name of a device node.

    /dev/nvme0n1p4 gives nvme0n1, /dev/md126p4 gives md126 and a
    whole disk gives its own name. None is returned when the device
    is not known to the kernel.
    """
    name = device_name(device)
    if name is None:
        return None
    path = os.path.join(SYS_CLASS_BLOCK, name)
    if not os.path.exists(path):
        return None
    if os.path.exists(os.path.join(path, 'partition')):
        # The directory of a partition sits inside the directory of
        # its disk, so the parent directory names the disk.
        return os.path.basename(os.path.dirname(os.path.realpath(path)))
    return name


def is_md(name):
    """Return True when the kernel name is an MD device."""
    if not name:
        return False
    return os.path.isdir(os.path.join(SYS_BLOCK, name, 'md'))


def running_members():
    """Return the set of kernel names that belong to a running MD array."""
    members = set()
    try:
        names = os.listdir(SYS_BLOCK)
    except OSError:
        return members
    for name in names:
        if not is_md(name):
            continue
        slaves = os.path.join(SYS_BLOCK, name, 'slaves')
        try:
            members.update(os.listdir(slaves))
        except OSError:
            pass
    return members


def has_raid_superblock(name):
    """Return True when blkid finds a RAID superblock on the device."""
    output = _run((BLKID, '-p', '-s', 'TYPE', '-o', 'value', os.path.join(DEV_DIR, name)))
    if output is None:
        return False
    return output.strip().endswith('_raid_member')


def has_imsm_signature(name):
    """Return True when the device ends with an Intel IMSM anchor block.

    This is a direct read of the on-disk signature, so it does not
    depend on the RAID formats known to the installed blkid.
    """
    size = _read_text(os.path.join(SYS_BLOCK, name, 'size'))
    if size is None or not size.isdigit():
        return False
    size = int(size) * 512
    try:
        handle = open(os.path.join(DEV_DIR, name), 'rb')
    except (IOError, OSError):
        return False
    try:
        for sector in SECTOR_SIZES:
            offset = size - 2 * sector
            if offset < 0:
                continue
            try:
                handle.seek(offset)
                block = handle.read(len(IMSM_SIGNATURE))
            except (IOError, OSError):
                continue
            if block == IMSM_SIGNATURE:
                return True
    finally:
        handle.close()
    return False


def is_raid_member(device):
    """Return True when the device is, or sits on, a RAID member."""
    name = device_name(device)
    disk = whole_disk(device)
    if name is None or disk is None:
        return False
    if is_md(disk):
        return False
    members = running_members()
    for candidate in (name, disk):
        if candidate in members:
            return True
    for candidate in (name, disk):
        if has_raid_superblock(candidate) or has_imsm_signature(candidate):
            return True
    return False


def candidates(label):
    """Return every device node that carries the label.

    The probe does not use the blkid cache, so partitions that were
    removed from the kernel are not reported.
    """
    if not label:
        return []
    output = _run((BLKID, '-c', os.devnull, '-o', 'device', '-t', 'LABEL=' + label))
    if output is None:
        return []
    found = []
    for line in output.splitlines():
        line = line.strip()
        if line and line not in found:
            found.append(line)
    return found


def resolve(label):
    """Return the device node to use for a label.

    Raises LookupError when no device carries the label and
    RaidMemberError when the label exists only on RAID members.
    Callers must treat RaidMemberError as "not found" and must not
    fall back to any other lookup.
    """
    found = candidates(label)
    if not found:
        raise LookupError('no device carries label %s' % label)
    on_md = [dev for dev in found if is_md(whole_disk(dev))]
    if on_md:
        return on_md[0]
    plain = [dev for dev in found if not is_raid_member(dev)]
    if not plain:
        raise RaidMemberError('label %s is only on RAID member disks: %s'
                              % (label, ' '.join(found)))
    return plain[0]


def choose(entries, label, device_of=None):
    """Pick one entry out of several that carry the same label.

    The entries come from a parsed blkid listing (see BlkidParser in
    onl.install). device_of returns the device node of an entry; by
    default the entry is the device node itself. The rules are the
    same as in resolve(). None is returned when nothing is usable.
    """
    if not entries:
        return None
    if device_of is None:
        device_of = lambda entry: entry
    on_md = [entry for entry in entries if is_md(whole_disk(device_of(entry)))]
    if on_md:
        return on_md[0]
    plain = [entry for entry in entries if not is_raid_member(device_of(entry))]
    if not plain:
        raise RaidMemberError('label %s is only on RAID member disks: %s'
                              % (label, ' '.join(device_of(e) for e in entries)))
    return plain[0]


def main(argv):
    """Resolve every label given on the command line; a debugging aid."""
    status = 0
    for label in argv:
        try:
            print('%s\t%s' % (label, resolve(label)))
        except LookupError as what:
            print('%s\t%s' % (label, what))
            status = 1
    return status


if __name__ == '__main__':
    import sys
    sys.exit(main(sys.argv[1:]))
