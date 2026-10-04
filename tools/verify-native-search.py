"""Exercise the verified EU4 province filter in this process, without launching a game.

Requires pefile and capstone. Native allocation, record movement and sorting are
replaced with test storage helpers; filtering and distance classification run
the original instructions against the built search adapter.
"""
import argparse
import ctypes as C
import hashlib
from pathlib import Path
import struct

import capstone
import pefile

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_HASH = "9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a"


class Storage(C.Union):
    _fields_ = [("inline", C.c_char * 16), ("pointer", C.c_void_p)]


class EngineString(C.Structure):
    _fields_ = [("storage", Storage), ("size", C.c_uint64), ("capacity", C.c_uint64)]


class Container(C.Structure):
    _fields_ = [("begin", C.c_void_p), ("end", C.c_void_p), ("capacity", C.c_void_p), ("exact", C.c_ubyte)]


Find = C.WINFUNCTYPE(C.c_uint64, C.c_void_p, C.c_uint64, C.c_uint64, C.c_void_p, C.c_uint64)
Distance = C.WINFUNCTYPE(C.c_int64, C.c_void_p, C.c_void_p)
Assign = C.WINFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p, C.c_uint64)
Unary = C.WINFUNCTYPE(None, C.c_void_p)
Move = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)
Compare = C.WINFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_size_t)
Filter = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_ubyte)


def verify(exe, build):
    raw = exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED_HASH:
        raise ValueError("Unsupported EU4 executable")
    pe = pefile.PE(data=raw)
    image = pe.get_memory_mapped_image()
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True
    kernel = C.WinDLL("kernel32", use_last_error=True)
    kernel.VirtualAlloc.argtypes = [C.c_void_p, C.c_size_t, C.c_uint32, C.c_uint32]
    kernel.VirtualAlloc.restype = C.c_void_p
    kernel.VirtualProtect.argtypes = [C.c_void_p, C.c_size_t, C.c_uint32, C.POINTER(C.c_uint32)]
    kernel.VirtualFree.argtypes = [C.c_void_p, C.c_size_t, C.c_uint32]
    kernel.FlushInstructionCache.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
    kernel.GetCurrentProcess.restype = C.c_void_p
    memory = kernel.VirtualAlloc(None, 16384, 0x3000, 0x04)
    if not memory:
        raise C.WinError(C.get_last_error())
    fixture = C.WinDLL(str(build / "search_fixture.dll"))
    fixture.SearchFixtureFind.argtypes = [C.c_size_t, C.c_void_p, C.c_uint64, C.c_uint64, C.c_void_p, C.c_uint64, C.c_void_p]
    fixture.SearchFixtureFind.restype = C.c_uint64
    fixture.SearchFixtureDistance.argtypes = [C.c_size_t, C.c_void_p, C.c_void_p, C.c_void_p]
    fixture.SearchFixtureDistance.restype = C.c_int64
    fixture.SearchFixtureSetOptions.argtypes = [C.c_uint, C.c_uint]
    fixture.SearchFixtureSetOptions.restype = None
    owned = []

    def assign_text(target, data):
        value = EngineString.from_address(target)
        C.memset(target, 0, 32)
        value.size = len(data)
        if len(data) < 16:
            C.memmove(target, data, len(data))
            value.capacity = 15
        else:
            buffer = C.create_string_buffer(data)
            owned.append(buffer)
            value.storage.pointer = C.addressof(buffer)
            value.capacity = len(data)

    @Assign
    def assign(target, source, size):
        assign_text(target, C.string_at(source, size))
        return target

    @Unary
    def destroy(target):
        C.memset(target, 0, 32)
        EngineString.from_address(target).capacity = 15

    @Unary
    def release(_):
        pass

    @Move
    def shift(first, end):
        C.memmove(first - 0x90, first, end - first)

    @Move
    def sort_records(_, __):
        pass

    @Compare
    def compare(left, right, size):
        a, b = C.string_at(left, size), C.string_at(right, size)
        return (a > b) - (a < b)

    @Find
    def find(name, length, start, query, query_length):
        return fixture.SearchFixtureFind(0x1141FEB, name, length, start, query, query_length, None)

    original_distance_address = memory + 0x1000

    @Distance
    def distance(name, query):
        return fixture.SearchFixtureDistance(0x1142192, name, query, original_distance_address)

    callbacks = {0x95110: assign, 0x95660: destroy, 0x17394F0: release,
                 0x1A35650: compare,
                 0x1145070: shift, 0x11456E0: sort_records,
                 0x17061A0: find, 0x171F880: distance}
    targets = {0x1A337F0: memory + 0x2000, 0x1703F30: memory + 0x2200}
    for index, (rva, callback) in enumerate(callbacks.items()):
        address = memory + 0x3000 + index * 16
        stub = b"\x48\xb8" + struct.pack("<Q", C.cast(callback, C.c_void_p).value) + b"\xff\xe0"
        C.memmove(address, stub, len(stub))
        targets[rva] = address

    def clone(rva, size, offset, skipped=()):
        data = bytearray(image[rva:rva + size])
        for instruction in decoder.disasm(data, rva):
            local = instruction.address - rva
            if instruction.address in skipped:
                if instruction.mnemonic != "call" or instruction.size != 5:
                    raise ValueError("Unexpected preprocessing instruction")
                data[local:local + 5] = b"\x90" * 5
            elif instruction.mnemonic == "call":
                target = instruction.operands[0].imm
                if instruction.bytes[0] != 0xE8 or target not in targets:
                    # The >32-result allocator and failure handlers are not
                    # part of this bounded fixture. Poison unexpected paths.
                    data[local:local + instruction.size] = b"\x0f\x0b" + b"\x90" * (instruction.size - 2)
                else:
                    struct.pack_into("<i", data, local + 1, targets[target] - (memory + offset + local + 5))
            elif instruction.group(capstone.CS_GRP_JUMP) and instruction.operands[0].type == capstone.CS_OP_IMM:
                if not rva <= instruction.operands[0].imm < rva + size:
                    raise ValueError("Unexpected external jump")
        C.memmove(memory + offset, bytes(data), size)

    try:
        # This helper uses the Windows stack-probe ABI, including its RAX input.
        probe = image[0x1A337F0:0x1A33880]
        C.memmove(memory + 0x2000, probe, len(probe))
        # The caller knows this leaf preserves R8/R9/R10; a C callback would
        # change those volatile registers and violate that observed contract.
        clone(0x1703F30, 0x10, 0x2200)
        clone(0x171F880, 0x160, 0x1000)
        clone(0x1141E00, 0x531, 0, (0x1141E9C, 0x1141EA6))
        previous = C.c_uint32()
        if not kernel.VirtualProtect(memory, 16384, 0x20, C.byref(previous)):
            raise C.WinError(C.get_last_error())
        kernel.FlushInstructionCache(kernel.GetCurrentProcess(), memory, 16384)
        native_filter = Filter(memory)
        names = ["法兰西", "奥地利", "重慶", "长安", "西藏", "勃兰登堡", "École Straße Long Province", "𠮷野"]
        cases = [("flx", "法兰西"), ("法lanxi", "法兰西"), ("adl", "奥地利"),
                 ("cq", "重慶"), ("changan", "长安"), ("xz", "西藏"),
                 ("bldb", "勃兰登堡"), ("兰西", "法兰西"), ("長安", "长安"),
                 ("𠮷", "𠮷野"), ("ecole", "École Straße Long Province"),
                 ("falanix", "法兰西"), ("falaxi", "法兰西"), ("falaanxi", "法兰西"),
                 ("falamxi", "法兰西"), ("congqin", "重慶")]
        count = 0
        for query, expected in cases:
            fixture.SearchFixtureSetOptions(1, int(query == "congqin"))
            for strict in (0, 1):
                records = C.create_string_buffer(0x90 * len(names))
                begin = C.addressof(records)
                for index, name in enumerate(names):
                    address = begin + index * 0x90
                    C.c_int32.from_address(address).value = 999
                    C.c_int32.from_address(address + 4).value = index + 1
                    for relative, text in [(0x10, str(index + 1)), (0x30, name), (0x50, name), (0x70, "")]:
                        assign_text(address + relative, text.encode("utf-8"))
                container = Container(begin, begin + len(names) * 0x90, begin + len(names) * 0x90, 0)
                needle = EngineString()
                assign_text(C.addressof(needle), query.encode("utf-8"))
                before = C.string_at(C.addressof(needle), 32)
                native_filter(C.byref(container), C.byref(needle), strict)
                if C.string_at(C.addressof(needle), 32) != before:
                    raise AssertionError("Native filter rewrote the original query")
                rows = []
                for address in range(container.begin, container.end, 0x90):
                    value = EngineString.from_address(address + 0x30)
                    text = C.string_at(address + 0x30 if value.capacity < 16 else value.storage.pointer, value.size).decode("utf-8")
                    identifier = C.c_int32.from_address(address + 4).value
                    if text != names[identifier - 1]:
                        raise AssertionError("Display name or province ID changed")
                    rows.append((text, C.c_int32.from_address(address).value))
                matches = [score for name, score in rows if name == expected]
                if not matches or (not strict and matches[0] > 1):
                    raise AssertionError((query, strict, expected, rows))
                count += 1
        print(f"PASS: {count} native province filter/rank cases; display names, query bytes and IDs preserved.")
    finally:
        kernel.VirtualFree(memory, 0, 0x8000)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-exe", type=Path, default=ROOT / "private/runtime/eu4.exe")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    verify(args.game_exe.resolve(), args.build_dir.resolve())
