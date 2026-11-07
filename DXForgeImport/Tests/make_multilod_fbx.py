# FBX SDK のサンプル box.fbx(ASCII の FBX 7)から、LOD グループが 2 つ(それぞれ LOD が 2 つ)ある ASCII の FBX を作る。
# バイナリの FBX をリポジトリに入れないために、テストの中で作る(import_cli_test.ps1、ops_cli_test.ps1)。
# 使い方: python make_multilod_fbx.py <box.fbx> <出力先.fbx>
import re
import sys

GEOMETRY_ID = "603770857"
MODEL_ID = "632370185"


class Builder:
    def __init__(self, geometry, model):
        self.geometry = geometry
        self.model = model
        self.objects = []
        self.connections = []
        self.next_id = 700000000

    def new_id(self):
        self.next_id += 1
        return str(self.next_id)

    def add_mesh(self, parent, name):
        geometry_id, model_id = self.new_id(), self.new_id()
        self.objects.append(self.geometry.replace(GEOMETRY_ID, geometry_id, 1))
        self.objects.append(self.model.replace(MODEL_ID, model_id, 1).replace("Model::Box01", "Model::" + name, 1))
        self.connections.append(f'    C: "OO",{model_id},{parent}\n')
        self.connections.append(f'    C: "OO",{geometry_id},{model_id}\n')

    def add_group(self, name, thresholds):
        # NOTE: get_lod_group の assert のとおり、しきい値の数は子の数 - 1 にする
        group_id, attribute_id = self.new_id(), self.new_id()
        levels = "".join(f'            P: "Thresholds|Level{i}", "Distance", "", "",{t},"cm"\n' for i, t in enumerate(thresholds))
        self.objects.append(
            f'    NodeAttribute: {attribute_id}, "NodeAttribute::{name}", "LodGroup" {{\n'
            "        Properties70:  {\n"
            '            P: "Thresholds", "Compound", "", ""\n'
            f"{levels}"
            "        }\n"
            '        TypeFlags: "LodGroup"\n'
            "    }\n")
        self.objects.append(
            f'    Model: {group_id}, "Model::{name}", "LODGroup" {{\n'
            "        Version: 232\n"
            "        Shading: T\n"
            '        Culling: "CullingOff"\n'
            "    }\n")
        self.connections.append(f'    C: "OO",{group_id},0\n')
        self.connections.append(f'    C: "OO",{attribute_id},{group_id}\n')
        for i in range(len(thresholds) + 1):
            self.add_mesh(group_id, f"{name}_LOD{i}")


def convert(text):
    geometry = re.search(rf"    Geometry: {GEOMETRY_ID},.*?\n    }}\n(?=    Model:)", text, re.S)
    model = re.search(rf"    Model: {MODEL_ID},.*?\n    }}\n(?=    AnimationStack:)", text, re.S)
    if not text.startswith("; FBX 7") or not geometry or not model:
        sys.exit("入力は FBX SDK のサンプルの box.fbx ではありません(ASCII の FBX 7 で、Box01 のメッシュがあるもの)")

    builder = Builder(geometry.group(0), model.group(0))
    builder.add_group("LodGroupA", [100])
    builder.add_group("LodGroupB", [200])

    text = text.replace(geometry.group(0), "".join(builder.objects), 1).replace(model.group(0), "", 1)
    text = text.replace(f'    C: "OO",{MODEL_ID},0\n', "".join(builder.connections), 1).replace(f'    C: "OO",{GEOMETRY_ID},{MODEL_ID}\n', "", 1)
    text = re.sub(r'(ObjectType: "Geometry" \{\n\s+Count: )1', r"\g<1>4", text)
    text = re.sub(r'(ObjectType: "Model" \{\n\s+Count: )1', r"\g<1>6", text)
    return text.replace(
        '    Count: 5\n    ObjectType: "GlobalSettings"',
        '    Count: 6\n    ObjectType: "NodeAttribute" {\n        Count: 2\n    }\n    ObjectType: "GlobalSettings"', 1)


def main():
    if len(sys.argv) != 3:
        sys.exit("使い方: python make_multilod_fbx.py <box.fbx> <出力先.fbx>")
    # NOTE: 改行はそろえて処理し、元の形式で書き戻す。ファイルの中の非 ASCII はそのまま通す
    text = open(sys.argv[1], encoding="utf-8", errors="surrogateescape", newline="").read()
    newline = "\r\n" if "\r\n" in text else "\n"
    converted = convert(text.replace("\r\n", "\n")).replace("\n", newline)
    open(sys.argv[2], "w", encoding="utf-8", errors="surrogateescape", newline="").write(converted)


if __name__ == "__main__":
    main()
