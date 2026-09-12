#!/usr/bin/env python3
"""Combine the documented Mac OS and MkLinux SCSI disks into one IDE disk.

The input disks must be offline. The Mac volume stays at its original offset;
Linux root and swap are appended and remain partitions 2 and 3. Only the root
device in lilo.conf and the root/swap devices in fstab are changed.
"""

import argparse
from pathlib import Path
import struct


def u16(data, offset, endian='>'):
    return struct.unpack_from(endian + 'H', data, offset)[0]


def u32(data, offset, endian='>'):
    return struct.unpack_from(endian + 'I', data, offset)[0]


def read_at(image, offset, size):
    image.seek(offset)
    data = image.read(size)
    if len(data) != size:
        raise ValueError('read extends past the disk image')
    return data


def partitions(image):
    header = read_at(image, 0, 512)
    if header[:2] != b'ER' or u16(header, 2) != 512:
        raise ValueError('expected an Apple disk image with 512-byte sectors')
    first = read_at(image, 512, 512)
    count = u32(first, 4)
    if first[:2] != b'PM' or not 1 <= count <= 63:
        raise ValueError('unsupported partition map')
    result = [read_at(image, i * 512, 512) for i in range(1, count + 1)]
    if any(p[:2] != b'PM' or u32(p, 4) != count for p in result):
        raise ValueError('inconsistent partition map')
    return header, result


def part_name(part):
    return part[16:48].split(b'\0')[0]


def part_type(part):
    return part[48:80].split(b'\0')[0]


def one(parts, predicate):
    matches = [p for p in parts if predicate(p)]
    if len(matches) != 1:
        raise ValueError('expected exactly one matching partition or file')
    return matches[0]


def edit_segments(image, segments, size, replacements):
    before = b''.join(read_at(image, offset, length) for offset, length in segments)[:size]
    if len(before) != size:
        raise ValueError('file needs unsupported overflow extents')
    after = before
    for old, new in replacements:
        if len(old) != len(new) or after.count(old) != 1:
            raise ValueError('expected one occurrence of ' + repr(old))
        after = after.replace(old, new)
    edits, consumed = [], 0
    for offset, length in segments:
        chunk = after[consumed:consumed + length]
        if chunk:
            edits.append((offset, chunk))
        consumed += length
    return edits


def hfs_edits(image, part):
    base = u32(part, 8) * 512
    mdb = read_at(image, base + 1024, 512)
    if mdb[:2] != b'BD':
        raise ValueError('expected an HFS Mac OS volume')
    block_size = u32(mdb, 20)
    block_base = base + u16(mdb, 28) * 512

    def extents(record, offset):
        pairs = [struct.unpack_from('>HH', record, offset + i * 4) for i in range(3)]
        return [(block_base + start * block_size, count * block_size)
                for start, count in pairs if count]

    catalog = b''.join(read_at(image, offset, length) for offset, length in extents(mdb, 150))
    if len(catalog) < u32(mdb, 146):
        raise ValueError('HFS catalog needs unsupported overflow extents')
    catalog = catalog[:u32(mdb, 146)]
    node_size = u16(catalog, 32)
    files = []
    for start in range(0, len(catalog), node_size):
        node = catalog[start:start + node_size]
        if len(node) != node_size or node[8] != 255:
            continue
        for i in range(u16(node, 10)):
            key = u16(node, node_size - 2 * (i + 1))
            name = node[key + 7:key + 7 + node[key + 6]]
            record = key + ((node[key] + 2) & ~1)
            if node[record] == 2 and name == b'lilo.conf':
                files.append((extents(node, record + 74), u32(node, record + 26)))
    segments, size = one(files, lambda _: True)
    return edit_segments(image, segments, size,
                         [(b'\rrootdev=/dev/sdb2\r', b'\rrootdev=/dev/hda2\r')])


def ext2_edits(image, part):
    base = u32(part, 8) * 512
    sb = read_at(image, base + 1024, 1024)
    if u16(sb, 56, '<') != 0xEF53:
        raise ValueError('expected an ext2 root filesystem')
    block_size = 1024 << u32(sb, 24, '<')
    inodes_per_group = u32(sb, 40, '<')
    inode_size = u16(sb, 88, '<') if u32(sb, 76, '<') else 128
    groups = base + (2 if block_size == 1024 else 1) * block_size

    def indirect(number, level):
        if not number:
            raise ValueError('sparse configuration files are unsupported')
        if not level:
            yield number
            return
        data = read_at(image, base + number * block_size, block_size)
        for child in struct.unpack('<%dI' % (block_size // 4), data):
            yield from indirect(child, level - 1)

    def file_segments(number):
        group, index = divmod(number - 1, inodes_per_group)
        table = u32(read_at(image, groups + group * 32, 32), 8, '<')
        inode = read_at(image, base + table * block_size + index * inode_size, inode_size)
        size = u32(inode, 4, '<')
        blocks = struct.unpack_from('<15I', inode, 40)
        segments = []
        for i, block in enumerate(blocks):
            if len(segments) * block_size >= size:
                break
            for number in indirect(block, max(0, i - 11)):
                segments.append((base + number * block_size, block_size))
                if len(segments) * block_size >= size:
                    break
        return segments, size

    inode_number = 2
    for component in (b'etc', b'fstab'):
        segments, size = file_segments(inode_number)
        directory = b''.join(read_at(image, offset, length) for offset, length in segments)[:size]
        offset, matches = 0, []
        while offset < len(directory):
            number, length, name_length = struct.unpack_from('<IHH', directory, offset)
            name_length &= 255
            if length < 8 or offset + length > len(directory):
                raise ValueError('invalid ext2 directory entry')
            if number and directory[offset + 8:offset + 8 + name_length] == component:
                matches.append(number)
            offset += length
        inode_number = one(matches, lambda _: True)
    segments, size = file_segments(inode_number)
    return edit_segments(image, segments, size,
                         [(b'/dev/sdb2', b'/dev/hda2'), (b'/dev/sdb3', b'/dev/hda3')])


def copy_region(source, target, source_offset, target_offset, length):
    source.seek(source_offset)
    target.seek(target_offset)
    while length:
        data = source.read(min(length, 1024 * 1024))
        if not data:
            raise ValueError('partition extends past the source image')
        if data == bytes(len(data)):
            target.seek(len(data), 1)
        else:
            target.write(data)
        length -= len(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('macos', type=Path)
    parser.add_argument('linux', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with args.macos.open('rb') as mac, args.linux.open('rb') as linux:
        header, mac_parts = partitions(mac)
        _, linux_parts = partitions(linux)
        mapping = one(mac_parts, lambda p: part_type(p) == b'Apple_partition_map')
        hfs = one(mac_parts, lambda p: part_type(p) == b'Apple_HFS')
        root = one(linux_parts, lambda p: part_name(p) == b'root' and part_type(p) == b'Apple_UNIX_SVR2')
        swap = one(linux_parts, lambda p: part_name(p) == b'swap' and part_type(p) == b'Apple_UNIX_SVR2')
        edits = hfs_edits(mac, hfs)
        root_edits = ext2_edits(linux, root)
        kept = [p for p in mac_parts if part_type(p) not in (b'Apple_partition_map', b'Apple_Free')]
        parts = [mapping, root, swap] + kept
        if u32(mapping, 8) != 1 or u32(mapping, 12) < len(parts):
            raise ValueError('not enough partition-map space')
        mac_size = args.macos.stat().st_size
        if mac_size % 512:
            raise ValueError('Mac image size is not a whole number of sectors')
        root_start = (mac_size // 512 + 2047) & ~2047
        swap_start = root_start + u32(root, 12)
        sectors = (swap_start + u32(swap, 12) + 2047) & ~2047
        new_header = bytearray(header)
        struct.pack_into('>I', new_header, 4, sectors)
        # Exclusive creation prevents overwriting an input or an existing disk.
        with args.output.open('x+b') as output:
            output.truncate(sectors * 512)
            copy_region(mac, output, 0, 0, mac_size)
            for part, start in ((root, root_start), (swap, swap_start)):
                copy_region(linux, output, u32(part, 8) * 512, start * 512, u32(part, 12) * 512)
            output.seek(0)
            output.write(new_header)
            for i, part in enumerate(parts, 1):
                entry = bytearray(part)
                struct.pack_into('>I', entry, 4, len(parts))
                if i in (2, 3):
                    struct.pack_into('>I', entry, 8, root_start if i == 2 else swap_start)
                output.seek(i * 512)
                output.write(entry)
                print(i, part_name(entry).decode('mac_roman'), part_type(entry).decode('ascii'),
                      'start', u32(entry, 8), 'sectors', u32(entry, 12))
            shift = (root_start - u32(root, 8)) * 512
            for offset, data in edits + [(offset + shift, data) for offset, data in root_edits]:
                output.seek(offset)
                output.write(data)
    print('Created', args.output, 'with root /dev/hda2 and swap /dev/hda3')


if __name__ == '__main__':
    main()
