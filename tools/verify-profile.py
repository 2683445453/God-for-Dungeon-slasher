"""Read-only native profile verification. Python 3 stdlib; never executes the game."""
import argparse
import hashlib
import pathlib
import re
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=pathlib.Path, required=True)
    parser.add_argument("--dump", type=pathlib.Path, help="Optional local Il2CppDumper dump.cs for signature checks")
    args = parser.parse_args()
    profile = (ROOT / "src/profile.h").read_text(encoding="utf-8")
    binary = (args.game_dir / "GameAssembly.dll").read_bytes()
    expected = re.search(r'AssemblySha256\[\] = L"([a-f0-9]+)"', profile)[1]
    assert hashlib.sha256(binary).hexdigest() == expected, "GameAssembly build hash mismatch"
    metadata = args.game_dir / "dungeonslasher_Data/il2cpp_data/Metadata/global-metadata.dat"
    expected = re.search(r'MetadataSha256\[\] = L"([a-f0-9]+)"', profile)[1]
    assert hashlib.sha256(metadata.read_bytes()).hexdigest() == expected, "Metadata build hash mismatch"
    pe = struct.unpack_from("<I", binary, 0x3C)[0]
    assert binary[pe:pe+4] == b"PE\0\0"
    machine, sections = struct.unpack_from("<HH", binary, pe+4)
    optional_size = struct.unpack_from("<H", binary, pe+20)[0]
    optional = pe+24
    assert machine == 0x8664 and struct.unpack_from("<H", binary, optional)[0] == 0x20B
    ranges = []
    for i in range(sections):
        at = optional+optional_size+i*40
        virtual_size, address, raw_size, offset = struct.unpack_from("<IIII", binary, at+8)
        ranges.append((address, raw_size, offset))

    def file_offset(rva, length=1):
        for address, size, offset in ranges:
            if address <= rva and rva+length <= address+size:
                return offset+rva-address
        raise AssertionError(f"RVA {rva:x} has no file-backed data")

    def data(rva, length):
        at = file_offset(rva, length)
        return binary[at:at+length]

    expression = re.compile(r'\{(?:"(?P<name>[^"\n]+)"\s*,\s*)?0x(?P<rva>[0-9a-fA-F]+)\s*,[^\{\n]*\{(?P<prefix>(?:0x[0-9a-fA-F]+\s*,?\s*)+)\}')
    records = []
    wallet_records = []
    for name in ("profile.h", "capacity-profile.inl", "wallet-profile.inl"):
        text = (ROOT / "src" / name).read_text(encoding="utf-8")
        for row in expression.finditer(text):
            rva = int(row["rva"], 16)
            prefix = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", row["prefix"]))
            assert len(prefix) == 16, f"Invalid profile prefix {row['name']}"
            assert data(rva, 16) == prefix, f"Prefix mismatch {row['name']} @ {rva:x}"
            records.append((row["name"], rva))
            if name == "wallet-profile.inl" and row["name"] and "/" in row["name"]:
                wallet_records.append((row["name"], rva))
    assert len(records) == 300, f"Expected full r19 profile, got {len(records)} records"
    assert len(wallet_records) == 53
    capacity_profile = (ROOT / "src/capacity-profile.inl").read_text(encoding="utf-8")
    operands = list(re.finditer(r'\{0x([0-9a-f]+),(\d+),(\d+),(-?\d+),-?\d+,\{', capacity_profile))
    assert len(operands) == 68
    for operand in operands:
        at, offset, width, value = int(operand[1],16), int(operand[2]), int(operand[3]), int(operand[4])
        assert width in (1,4) and int.from_bytes(data(at+offset,width),"little",signed=True) == value, f"Capacity operand mismatch at {at:x}"
    # This is the actual inlined RNG invocation followed by cmp eax, ebx.
    wallet_profile = (ROOT / "src/wallet-profile.inl").read_text(encoding="utf-8")
    roll = int(re.search(r'ChallengeRollReturnRva=0x([0-9a-f]+)', wallet_profile)[1], 16)
    assert data(roll-2, 4) in (bytes.fromhex("ff d0 3b c3"), bytes.fromhex("ff d0 39 d8"))
    # Independently tie return-address filters and the generic singleton usage
    # to the actual machine instructions, not just matching byte snapshots.
    sites = dict(records)
    def near_call(at, destination):
        assert data(at,1) == b"\xe8" and at+5+struct.unpack("<i",data(at+1,4))[0] == destination, f"Unexpected native callee at {at:x}"

    for name in ("ArtifactSaveCallRva", "RunGemSaveCallRva"):
        value = int(re.search(name+r' = 0x([0-9a-f]+)', profile)[1], 16)
        expected = sites["DataController.AutoSave.ArtifactState" if name.startswith("Artifact") else "ClearScreen.GetClearReward.RunGem"]
        assert value == expected
    near_call(sites["DataController.AutoSave.ArtifactState"],sites["ArtifactManager.GetArtifactStatesForAutoSave"])
    for name in ("ClearScreen.GetClearReward.RunGem","ClearScreen.GetClearReward.QuestGem"):
        near_call(sites[name],sites["DataController.SaveGem/2"])
    for array, getter in (("ArtifactInventoryCalls","CapacitySix.Scoped"),("SkillNormalCalls","CapacitySix.Scoped"),("SkillInventoryCalls","CapacitySix.Scoped"),("SkillEquipmentCalls","CapacityEight.Scoped")):
        block = re.search(array+r'\[\] = \{(.*?)\n\};',capacity_profile,re.S)[1]
        for row in expression.finditer(block):
            near_call(int(row["rva"],16),sites[getter])
    lea = sites["ClearScreen.DataControllerMetadata"]
    usage = lea+7+struct.unpack("<i", data(lea+3,4))[0]
    assert data(lea,3) == b"\x48\x8d\x0d"
    near_call(lea+7,sites["InitializeRuntimeMetadata"])
    assert usage == int(re.search(r'DataControllerMethodUsageRva = 0x([0-9a-f]+)', profile)[1],16)
    for name in (n for n in sites if n and n.endswith("damageFont")):
        at = sites[name]
        near_call(at,sites["Monster.MakeDamageFont"])
    export_rva = struct.unpack_from("<I", binary, optional+112)[0]
    export = file_offset(export_rva, 40)
    count, names_rva = struct.unpack_from("<II", binary, export+24)[0], struct.unpack_from("<I", binary, export+32)[0]
    exports = set()
    for i in range(count):
        rva = struct.unpack("<I", data(names_rva+i*4, 4))[0]
        at = file_offset(rva)
        exports.add(binary[at:binary.index(b"\0", at)].decode("ascii"))
    managed = (ROOT / "src/managed-runtime.h").read_text(encoding="utf-8")
    required = set("il2cpp_"+value for value in re.findall(r'DS_EXPORT\(\w+,"([^"]+)"\)', managed))
    assert len(required) == 16 and required <= exports, "Managed runtime exports missing"
    if args.dump:
        dump = args.dump.read_text(encoding="utf-8-sig")
        methods = dict((int(rva, 16), declaration) for rva, declaration in re.findall(r'// RVA: 0x([0-9A-F]+)[^\n]*\n\s*([^\n]+)', dump))
        for name, rva in wallet_records:
            method, argc = name.split(".", 1)[1].split("/")
            declaration = methods[rva]
            match = re.search(r'\b'+re.escape(method)+r'\((.*?)\)', declaration)
            assert match and (0 if not match[1] else len(match[1].split(","))) == int(argc), (name, declaration)
        for space, name in (("SkillTable", "SkillData"), ("ArtifactTable", "ArtifactData"), ("NSP", "RuneInstance"), ("NSP", "RuneDropService")):
            assert re.search(r'// Namespace: '+space+r'\s*\n(?:\[[^\n]*\]\n)*public (?:class|static class) '+name+r'\b', dump), (space, name)
        def field(type_name, name, offset):
            block = re.search(r'(?m)^public (?:abstract )?class '+re.escape(type_name)+r'\b[^\n]*\n\{(.*?)^\}',dump,re.S|re.M)[1]
            assert re.search(r'\b'+re.escape(name)+r'; // 0x'+f'{offset:X}'+r'\b',block), (type_name,name,offset)
        for name, offset in (("NowDashCount",0x630),("moreJumpCount",0x4c4),("weaponSkillCoolTime",0x620),("nowWeaponSkillCount",0x628)):
            field("PlayerMovement",name,offset)
        for name, offset in (("SkillAutoSaveData",0x1cb0),("skillHasteDict",0x1cb8),("ArtifactAutoSaveData",0x1cd0),("ArtifactNum",0x1cd8),("ArtifactStates",0x1ce0)):
            field("AutoSaveDataContainer",name,offset)
        field("Skills","SkillCooldown",0x70)
        field("Delegate","method_ptr",0x10)
        field("Delegate","m_target",0x20)
        assert "public const CharacterName Slime = 22;" in dump
    print(f"PASS: exact build hashes, {len(records)} native prefixes, 68 capacity operands and scoped/native callees, {len(required)} IL2CPP exports, actual challenge RNG call/compare" + (", 53 wallet overload signatures, catalog namespaces and save fields" if args.dump else ""))


if __name__ == "__main__":
    main()
