import os
import struct
import argparse
from gclib.gcm import GCM
from gclib.rarc import RARC, RARCFileEntry, RARCNode
from gclib.yaz0_yay0 import Yaz0
from io import BytesIO
from typing import List, Union
from pathlib import Path
import json
from configparser import ConfigParser

padding_bytes = [b'T', b'h', b'i', b's', b' ', b'i', b's', b' ', b'p', b'a', b'd', b'd', b'i', b'n', b'g',
                 b' ', b'd', b'a', b't', b'a', b' ', b't', b'o', b' ', b'a', b'l', b'i', b'g', b'n', b' ']


global_material_ids: dict[str, bytes] = {}
global_element_counter = int(0)
blo_material_init_idxs: dict[str, dict[str, int]] = {}
elements_size = int(0)


def calc_keycode(name: str) -> int:
    key_code = 0
    name_bytes = name.encode('shift_jis')
    for b in name_bytes:
        key_code = key_code * 3 + b
        key_code &= 0xFFFFFFFF
    return key_code & 0xFFFF


# Bytes to int helper function
def bytes_to_int(data: bytes) -> int:
    return int.from_bytes(data, byteorder='big', signed=True)


def parse_blo(blo_bytes: BytesIO):
    return BLO(blo_bytes)


def list_tree(node, depth=0):
    indent = "  " * depth

    if hasattr(node, "base_pan2"):
        name = node.base_pan2.info_tag.decode().rstrip("\x00")
    else:
        name = node.info_tag.decode().rstrip("\x00")

    print(f"{indent}{node.magic.decode()} : {name}")

    for child in getattr(node, "child_nodes", []):
        list_tree(child, depth + 1)


class BLO:
    def __init__(self, data: BytesIO):
        self.name = str()
        data.seek(0)
        self.tag = data.read(4)
        self.type = data.read(4)
        self.size = bytes_to_int(data.read(4))
        self.blocks = bytes_to_int(data.read(4))
        self.header_padding = data.read(16)
        self.inf1_section = INF1_Section(data)
        self.tex1_section = TEX1_Section(data)
        self.fnt1_section = FNT1_Section(data)
        self.mat1_section = MAT1_Section(data)
        self.elements = PAN2(data)
        self.padding = data.read(self.size - data.tell())

    def to_bytes(self):
        out = bytearray()
        out += self.tag
        out += self.type
        out += self.size.to_bytes(4, 'big')
        out += self.blocks.to_bytes(4, 'big')
        out += self.header_padding
        out += self.inf1_section.to_bytes()
        out += self.tex1_section.to_bytes()
        out += self.fnt1_section.to_bytes()
        out += self.mat1_section.to_bytes()
        out += self.elements.to_bytes()
        out += self.padding
        return out

    def rebuild_blo(self, path: str):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        data = self.to_bytes()
        with open(path, "wb") as f:
            f.write(data)

    @classmethod
    def from_json_file(cls, path: Path):
        with open(path, "r", encoding="utf-8") as f:
            json_data = json.load(f)

        inf1_json = json_data[0]
        section_list = json_data[1]

        self = cls.__new__(cls)

        # Header
        self.tag = b'SCRN'
        self.type = b'blo2'
        self.size = 0
        self.blocks = 0
        self.header_padding = b'SVR1'
        self.header_padding += b'\xFF' * 12

        # INF1 Section
        self.inf1_section = INF1_Section.from_json(inf1_json)

        for section in section_list:
            if section["type"] == "TEX1":
                self.tex1_section = TEX1_Section.from_json(section)
            elif section["type"] == "FNT1":
                self.fnt1_section = FNT1_Section.from_json(section)
            elif section["type"] == "MAT1":
                self.mat1_section = MAT1_Section.from_json(section, bytes_to_int(self.tex1_section.offset_count), self.tex1_section)
            else:
                break
        self.elements = PAN2.from_json(section_list[3], section_list[4], True, False)

        self.size = self.get_total_size() - 24
        self.blocks = self.get_blocks()

        self.padding = b''
        i = 0
        while self.size % 32 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1
        return self

    def get_total_size(self):
        size = int(0)
        size += 32
        size += self.inf1_section.size
        size += self.tex1_section.header.section_size
        size += self.fnt1_section.header.section_size
        size += self.mat1_section.size
        size += elements_size
        return size

    def get_blocks(self):
        blocks = int(4)
        blocks += global_block_counter
        return blocks

    def get_new_block_num(self):
        global global_block_counter
        global_block_counter = int(4)

        def calc_blocks(node):
            global global_block_counter
            if hasattr(node, "child_nodes"):
                if len(node.child_nodes) > 0:
                    global_block_counter += 2
                    for child in node.child_nodes:
                        calc_blocks(child)
            else:
                global_block_counter += 2
            return global_block_counter

        return calc_blocks(self.elements)


    def edit_from_json(self, data):
        global global_element_counter
        global elements_size
        size_change = int(0)
        num_of_elements_added = int(0)

        mats_to_delete: List[int] = []
        for entry in data[1]:
            match entry["action"]:
                # to-do: add material for each PIC2 or TBX2 element added
                # that includes:
                # init data itself
                # idx in the correct position
                # mat name header hash, offset, and name in the table
                case "add":
                    num_of_elements_added += 1
                    node = depth_first_search(self.elements, entry)

                    match entry["type"]:
                        case "PAN2":
                            new_pan2 = PAN2.new_pan2(entry, False)
                            node.child_nodes.append(new_pan2)
                            # elements_size += new_pan2.size + new_pan2.bgn1_tag_size + new_pan2.end_tag_size

                        case "PIC2":
                            new_pic2 = PIC2.new_pic2(entry)
                            # elements_size += new_pic2.size
                            node.child_nodes.append(new_pic2)
                        
                        case "TBX2":
                            new_tbx2 = TBX2.new_tbx2(entry)
                            # elements_size += new_tbx2.size
                            node.child_nodes.append(new_tbx2)

                case "delete":
                    node = depth_first_search(self.elements, entry)

                    nodes_to_keep: List[PAN2 | PIC2 | TBX2] = []
                    for child in node.child_nodes:
                        match child.magic.decode():
                            case "PAN2":
                                if entry["element_name"] == child.info_tag.decode().strip("\x00"):
                                    def get_size(node):
                                        out = 0

                                        if node.magic.decode() == "PAN2":
                                            out += node.size + node.bgn1_tag_size + node.end_tag_size
                                        else:
                                            out += node.size

                                        for child in getattr(node, "child_nodes", []):
                                            out += get_size(child)

                                        return out

                                    print(f"Deleted node: {child.info_tag.decode()}")

                                    def gather_mats_to_delete(node):
                                        if hasattr(node, "child_nodes"):
                                            if len(node.child_nodes) > 0:
                                                for child in node.child_nodes:
                                                    if child.magic.decode() == "PAN2":
                                                        gather_mats_to_delete(child)
                                                        continue
                                                    mats_to_delete.append(bytes_to_int(child.material_num))
                                    gather_mats_to_delete(child)
                                    # elements_size -= get_size(child)
                                else:
                                    nodes_to_keep.append(child)

                            case "PIC2" | "TBX2":
                                if entry["element_name"] == child.base_pan2.info_tag.decode().strip("\x00"):
                                    print(f"Deleted node: {child.base_pan2.info_tag.decode()}")
                                    # elements_size -= child.size
                                    mats_to_delete.append(bytes_to_int(child.material_num))
                                else:
                                    nodes_to_keep.append(child)
                    node.child_nodes = nodes_to_keep
                    # for mat in mats_to_delete:
                    #     print(mat)

                case "edit":
                    node = depth_first_search(self.elements, entry)
                    for child in node.child_nodes:
                        match child.magic.decode():
                            case "PAN2":
                                if entry["element_name"] == child.info_tag.decode().strip("\x00"):
                                    for field in entry["fields_to_edit"]:
                                        set_new_value(child, field, entry[f"{field}-new_value"], entry[f"{field}-type"])

                            case "PIC2" | "TBX2":
                                if entry["element_name"] == child.base_pan2.info_tag.decode().strip("\x00"):
                                    for field in entry["fields_to_edit"]:
                                        set_new_value(child, field, entry[f"{field}-new_value"], entry[f"{field}-type"])

        # self.mat1_section.adjust_mat1(mats_to_delete)
        # self.mat1_section.get_offsets()
        # self.mat1_section.dither_section.size = len(self.mat1_section.dither_section.dither)
        # self.mat1_section.dither_section.padding = b''
        # orig_end_padding_size = len(self.mat1_section.end_padding)
        # self.mat1_section.end_padding = b''
        # self.mat1_section.size = self.mat1_section.get_size_no_end_padding()
        # self.mat1_section.get_end_padding(self.mat1_section.size)
        # self.mat1_section.size += len(self.mat1_section.end_padding)
        # size_change += orig_end_padding_size - len(self.mat1_section.end_padding)
        # global_element_counter = 0
        # iterate_element_counter(self.elements)
        def set_element_mat_no(node, data):
            if hasattr(node, "child_nodes"):
                if len(node.child_nodes) > 0:
                    for child in node.child_nodes:
                        set_element_mat_no(child, data)
            elif hasattr(node, "base_pan2"):
                target_mat_name = node.base_pan2.info_tag.decode()
                for i, mat_name in enumerate(self.mat1_section.mat_name_table_section.mat_names):
                    if target_mat_name in mat_name.decode():
                        node.material_num = i.to_bytes(2, 'big')
                        print(f"{target_mat_name} - {i}")

        def set_new_mat_no(node, data):
            if hasattr(node, "child_nodes"):
                if len(node.child_nodes) > 0:
                    for child in node.child_nodes:
                        set_new_mat_no(child, data)
            elif hasattr(node, "base_pan2"):
                for entry in data[1]:
                    if "mat_name" in entry:
                        if hasattr(node, "base_pan2"):
                            target_element_name = node.base_pan2.info_tag.decode()
                            if target_element_name in entry["element_name"]:
                                if entry["action"] == "add" or entry["action"] == "edit":
                                    target_mat_name = entry["mat_name"]
                                    for i, mat_name in enumerate(self.mat1_section.mat_name_table_section.mat_names):
                                        if target_mat_name in mat_name.decode():
                                            node.material_num = i.to_bytes(2, 'big')
                                            print(f"{target_mat_name} - {i}")

        set_element_mat_no(self.elements, data)
        set_new_mat_no(self.elements, data)
        self.blocks = self.get_new_block_num()

        # The code below gets an alphabetically sorted list of all the PAN2 nodes whose child nodes
        # make up the PIC2 nodes in the tree
        def collect_all_pan2_parents_with_pic2(node, out=None):
            if out is None:
                out = []

            if node.magic.decode() == "PAN2":
                has_pic2 = any(
                    child.magic.decode() == "PIC2" or
                    any(grandchild.magic.decode() == "PIC2" for grandchild in getattr(child, "child_nodes", []))
                    for child in getattr(node, "child_nodes", [])
                )

                if has_pic2:
                    out.append(node)

            for child in getattr(node, "child_nodes", []):
                collect_all_pan2_parents_with_pic2(child, out)

            return out
        all_pan2_nodes = collect_all_pan2_parents_with_pic2(self.elements)
        all_pan2_nodes.sort(key=lambda n: n.info_tag.decode().strip("\x00").lower())

        def remove_nested_nodes(nodes, exclude_tags=("ROOT", "n_all")):
            def is_descendant_of(node, potential_parent):
                for child in getattr(potential_parent, "child_nodes", []):
                    if child.magic.decode().strip('\x00') == "PAN2":
                        if child.info_tag == node.info_tag:
                            return True
                        if is_descendant_of(node, child):
                            return True
                return False

            filtered = []
            for node in nodes:
                tag = node.info_tag.decode().strip("\x00")
                if tag in exclude_tags:
                    continue

                # Only include node if it's not a descendant of any OTHER node in the list
                if not any(
                    node != other_node and is_descendant_of(node, other_node)
                    for other_node in nodes
                    if other_node.info_tag.decode().strip("\x00") not in exclude_tags
                ):
                    filtered.append(node)

            return filtered
        node_list = remove_nested_nodes(all_pan2_nodes)
        n_all_node = next((n for n in all_pan2_nodes if n.info_tag.decode().strip("\x00") == "n_all"), None)
        if n_all_node:
            has_pic2_child = any(
                getattr(child, "magic", None) and child.magic.decode().strip("\x00") == "PIC2"
                for child in getattr(n_all_node, "child_nodes", [])
            )
            if has_pic2_child:
                name_to_insert = "n_all"
                index = 0
                for i, n in enumerate(node_list):
                    if n.info_tag.decode().strip("\x00").lower() > name_to_insert:
                        index = i
                        break
                else:
                    index = len(node_list)
                node_list.insert(index, n_all_node)
        # For each node:
        # 1) Check if there are any direct PIC2 children. If so, alphabetically add to the list
        # 2) Find any PAN2 elements that are children of the current node and sort alphabetically
        # 3) Recurse and repeat for each PAN2 element, while ensuring duplicate PIC2 elements are not added to the list
        pic2_list: List[PIC2] = []

        def get_pic2s(node):
            per_pan_pic2s: List[PIC2] = []
            for child in node.child_nodes:
                if child.magic.decode() == "PIC2":
                    per_pan_pic2s.append(child)
                elif child.magic.decode() == "PAN2":
                    continue
            per_pan_pic2s.sort(key=lambda n: n.base_pan2.info_tag.decode().strip("\x00").lower())
            for pic2 in per_pan_pic2s:
                add_pic2 = True
                for cur_pic2 in pic2_list:
                    if pic2.base_pan2.info_tag.decode() == cur_pic2.base_pan2.info_tag.decode():
                        add_pic2 = False
                if (add_pic2):
                    pic2_list.append(pic2)
            pan2s: List[PAN2] = []
            for child in node.child_nodes:
                if child.magic.decode() == "PAN2":
                    pan2s.append(child)
            pan2s.sort(key=lambda n: n.info_tag.decode().strip("\x00").lower())
            for pan2 in pan2s:
                get_pic2s(pan2)

        for node in node_list:
            get_pic2s(node)

        # For each PIC2 element in pic2_list, find in the self.elements node tree the cooresponding element.
        # Then, add four instances of unk_idx to the pic2 element's field_0x8, incrementing by one for each value AFTER
        # being appended to field_0x8.
        global cur_pic2_idx
        cur_pic2_idx = int(0)
        global unk_idx
        unk_idx = int(0)

        def iterate_unk_idx(node) -> bool:
            global cur_pic2_idx
            global unk_idx
            if cur_pic2_idx >= len(pic2_list):
                return True

            if node.magic.decode() == "PAN2":
                for child in node.child_nodes:
                    iterate_unk_idx(child)
            elif node.magic.decode() == "PIC2":
                if node.base_pan2.info_tag.decode() == pic2_list[cur_pic2_idx].base_pan2.info_tag.decode():
                    for i in range(4):
                        node.field_0x8[i] = unk_idx.to_bytes(2, 'big')
                        unk_idx += 1
                    i = 0
                    cur_pic2_idx += 1
                    return True
            return False

        while cur_pic2_idx < len(pic2_list):
            cont_flg = False
            while not cont_flg:
                cont_flg = iterate_unk_idx(self.elements)

        elements_size = int(8)
        def get_elements_size(node):
            global elements_size
            if hasattr(node, "child_nodes"):
                elements_size += node.size + node.bgn1_tag_size + node.end_tag_size
                if (len(node.child_nodes)) > 0:
                    for child in node.child_nodes:
                        get_elements_size(child)
            else:
                elements_size += node.size
        get_elements_size(self.elements)


def set_new_value(object, field, value, type):
    if hasattr(object, field):
        match type:
            case "float" | "string" | "int":
                setattr(object, field, value)
            case "u8":
                setattr(object, field, value.to_bytes(1, 'big'))
            case "u16":
                setattr(object, field, value.to_bytes(2, 'big'))
    elif hasattr(object.base_pan2, field):
        match type:
            case "float" | "string" | "int":
                setattr(object.base_pan2, field, value)
            case "u8":
                setattr(object.base_pan2, field, value.to_bytes(1, 'big'))
            case "u16":
                setattr(object.base_pan2, field, value.to_bytes(2, 'big'))
blos: List[BLO] = []


class INF1_Section:
    def __init__(self, data: BytesIO):
        self.magic = data.read(4)
        self.size = bytes_to_int(data.read(4))
        self.width = data.read(2)
        self.height = data.read(2)
        self.values: List[bytes] = [data.read(1), data.read(1), data.read(1), data.read(1)]
        padding_size = self.size - 16
        self.padding = data.read(padding_size)

    def to_bytes(self):
        out = bytearray()
        out += self.magic
        out += self.size.to_bytes(4, 'big')
        out += self.width
        out += self.height
        for value in self.values:
            out += value
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        self.magic = b'INF1'
        self.size = 32
        self.width = int(obj["width"]).to_bytes(2, 'big')
        self.height = int(obj["height"]).to_bytes(2, 'big')
        self.values = []
        for i, value in enumerate(obj["values"]):
            self.values.insert(i, int(value).to_bytes(1, 'big'))
        self.padding = b'This is padding '

        return self


# TEX1 ---------------------------------------------------------------------------------
class TEX1_Section:
    def __init__(self, data: BytesIO):
        self.header = TEX1_Header(data)
        self.size_after_header = self.header.section_size - self.header.header_size
        self.offset_count = data.read(2)
        self.offsets: List[bytes] = []
        self.texture_refs: List[TEX1_Reference] = []
        self.padding = bytes()
        if bytes_to_int(self.header.texture_count) > 0:
            for i in range(int.from_bytes(self.offset_count, byteorder='big')):
                self.offsets.insert(i, data.read(2))
            for i in range(int.from_bytes(self.offset_count, byteorder='big')):
                if i + 1 < int.from_bytes(self.offset_count, byteorder='big'):
                    ref = TEX1_Reference()
                    ref.res_type = data.read(1)
                    ref.texture_name_length = data.read(1)
                    ref.texture = data.read(bytes_to_int(ref.texture_name_length))
                    self.texture_refs.insert(i, ref)
                else:
                    ref = TEX1_Reference()
                    ref.res_type = data.read(1)
                    ref.texture_name_length = data.read(1)
                    size = self.size_after_header - int.from_bytes(self.offsets[i], byteorder='big') - 2
                    ref.texture = data.read(size)
                    self.texture_refs.insert(i, ref)
        else:
            self.padding = data.read(14)

    def to_bytes(self):
        out = bytearray()
        out += self.header.to_bytes()
        out += self.offset_count

        for offset in self.offsets:
            out += offset

        for ref in self.texture_refs:
            out += ref.res_type
            out += ref.texture_name_length
            out += ref.texture

        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Header
        self.header = TEX1_Header.__new__(TEX1_Header)
        self.header.magic = b'TEX1'
        self.header.texture_count = len(obj["references"]).to_bytes(2, 'big')
        self.header.padding = b'\xFF'b'\xFF'
        self.header.header_size = int(16)

        self.offset_count = len(obj["references"]).to_bytes(2, 'big')

        # Initialize Texture Offsets
        self.offsets = []
        for i in range(bytes_to_int(self.header.texture_count)):
            self.offsets.insert(i, b'\x00'b'\x00')

        # Texture references
        self.texture_refs = [TEX1_Reference.from_json(name) for name in obj["references"]]
        for i, ref in enumerate(self.texture_refs):
            ref.res_type = b'\x02'
            ref.texture_name_length = len(obj["references"][i]).to_bytes(1, 'big')

        # Calculate section size
        size = self.header.header_size
        for i in range(bytes_to_int(self.header.texture_count)):
            size += (bytes_to_int(self.texture_refs[i].texture_name_length) + 2)
        size += bytes_to_int(self.header.texture_count) * 2
        self.header.section_size = size + 2

        # Fill offset values
        self.get_offsets()

        # Calculate Padding
        self.padding = b''
        i = 0
        while self.header.section_size % 32 != 0:
            self.header.section_size += 1
            self.padding += padding_bytes[i]
            i += 1

        return self

    def get_offsets(self):
        first_offset = (bytes_to_int(self.header.texture_count) * 2) + 2
        self.offsets[0] = first_offset.to_bytes(2, 'big')
        for i, offset in enumerate(self.offsets):
            if i == 0:
                continue
            self.offsets[i] = ((bytes_to_int(self.texture_refs[i - 1].texture_name_length) + 2) + bytes_to_int(self.offsets[i - 1])).to_bytes(2, 'big')


class TEX1_Header:
    def __init__(self, data: BytesIO):
        self.magic = data.read(4)
        self.section_size = bytes_to_int(data.read(4))
        self.texture_count = data.read(2)
        self.padding = data.read(2)
        self.header_size = bytes_to_int(data.read(4))

    def to_bytes(self):
        out = bytearray()
        out += self.magic
        out += self.section_size.to_bytes(4, 'big')
        out += self.texture_count
        out += self.padding
        out += self.header_size.to_bytes(4, 'big')
        return out


class TEX1_Reference:
    def __init__(self):
        self.res_type = bytes()
        self.texture_name_length = bytes()
        self.texture = bytes()

    @classmethod
    def from_json(cls, name: str):
        self = cls.__new__(cls)
        self.res_type = b'\x02'
        self.texture_name_length = b'\x00'
        self.texture = name.encode('ascii')
        return self
# --------------------------------------------------------------------------------------


# FNT1 ---------------------------------------------------------------------------------
class FNT1_Section:
    def __init__(self, data: BytesIO):
        self.header = FNT1_Header(data)
        self.size_after_header = self.header.section_size - self.header.header_size
        self.offset_count = data.read(2)
        self.offsets: List[bytes] = []
        self.font_refs: List[FNT1_Reference] = []
        self.padding = bytes()
        if bytes_to_int(self.header.font_count) > 0:
            for i in range(int.from_bytes(self.offset_count, byteorder='big')):
                self.offsets.insert(i, data.read(2))
            for i in range(int.from_bytes(self.offset_count, byteorder='big')):
                if i + 1 < int.from_bytes(self.offset_count, byteorder='big'):
                    ref = FNT1_Reference()
                    ref.res_type = data.read(1)
                    ref.font_name_length = data.read(1)
                    ref.font = data.read(bytes_to_int(ref.font_name_length))
                    self.font_refs.insert(i, ref)
                else:
                    ref = FNT1_Reference()
                    ref.res_type = data.read(1)
                    ref.font_name_length = data.read(1)
                    size = self.size_after_header - int.from_bytes(self.offsets[i], byteorder='big') - 2
                    ref.font = data.read(size)
                    self.font_refs.insert(i, ref)
        else:
            self.padding = data.read(14)

    def to_bytes(self):
        out = bytearray()
        out += self.header.to_bytes()
        out += self.offset_count

        for offset in self.offsets:
            out += offset

        for ref in self.font_refs:
            out += ref.res_type
            out += ref.font_name_length
            out += ref.font

        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Header
        self.header = FNT1_Header.__new__(FNT1_Header)
        self.header.magic = b'FNT1'
        self.header.font_count = len(obj["references"]).to_bytes(2, 'big')
        self.header.padding = b'\xFF'b'\xFF'
        self.header.header_size = int(16)

        self.offset_count = len(obj["references"]).to_bytes(2, 'big')

        # Initialize font offsets
        self.offsets = []
        for i in range(bytes_to_int(self.header.font_count)):
            self.offsets.insert(i, b'\x00'b'\x00')

        # Font references
        self.font_refs = [FNT1_Reference.from_json(name) for name in obj["references"]]
        for i, ref in enumerate(self.font_refs):
            ref.res_type = b'\x02'
            ref.font_name_length = len(obj["references"][i]).to_bytes(1, 'big')

        # Calculate section size
        size = self.header.header_size
        for i in range(bytes_to_int(self.header.font_count)):
            size += (bytes_to_int(self.font_refs[i].font_name_length) + 2)
        size += bytes_to_int(self.header.font_count) * 2
        self.header.section_size = size + 2

        # Fill offset values
        self.get_offsets()

        # Calculate padding
        self.padding = b''
        i = 0
        while self.header.section_size % 32 != 0:
            self.header.section_size += 1
            self.padding += padding_bytes[i]
            i += 1

        return self

    def get_offsets(self):
        first_offset = (bytes_to_int(self.header.font_count) * 2) + 2
        self.offsets[0] = first_offset.to_bytes(2, 'big')
        for i, offset in enumerate(self.offsets):
            if i == 0:
                continue
            self.offsets[i] = ((bytes_to_int(self.font_refs[i - 1].font_name_length) + 2) + bytes_to_int(self.offsets[i - 1])).to_bytes(2, 'big')


class FNT1_Header:
    def __init__(self, data: BytesIO):
        self.magic = data.read(4)
        self.section_size = bytes_to_int(data.read(4))
        self.font_count = data.read(2)
        self.padding = data.read(2)
        self.header_size = bytes_to_int(data.read(4))

    def to_bytes(self):
        out = bytearray()
        out += self.magic
        out += self.section_size.to_bytes(4, 'big')
        out += self.font_count
        out += self.padding
        out += self.header_size.to_bytes(4, 'big')
        return out


class FNT1_Reference:
    def __init__(self):
        self.res_type = bytes()
        self.font_name_length = bytes()
        self.font = bytes()

    @classmethod
    def from_json(cls, name: str):
        self = cls.__new__(cls)
        self.res_type = b'\x02'
        self.font_name_length = b'\x00'
        self.font = name.encode('ascii')
        return self
# --------------------------------------------------------------------------------------


# MAT1 ---------------------------------------------------------------------------------
class MAT1_Section:
    def __init__(self, data: BytesIO):
        self.magic = data.read(4)
        self.size = bytes_to_int(data.read(4))
        self.material_count = data.read(2)
        self.padding = data.read(2)
        self.offsets = MAT1_Section_Offsets(data)
        self.mat_init_section = Mat_Init_Data_Section(data, self.offsets)
        self.mat_init_idx_section = Mat_Init_Data_Idx_Section(data, self.offsets)
        self.mat_name_table_section = Mat_Name_Table_Section(data, self.offsets, self.mat_init_idx_section)
        self.ind_init_data_section = Ind_Init_Data_Section(data, self.offsets)
        self.cull_modes = Cull_Mode_Section(data, self.offsets, self.mat_init_section)
        self.mat_colors = Mat_Color_Section(data, self.offsets, self.mat_init_section)
        self.color_chan_num_section = Color_Chan_Num_Section(data, self.offsets, self.mat_init_section)
        self.color_chan_info_section = Color_Chan_Info_Section(data, self.offsets, self.mat_init_section)
        self.tex_gen_num_section = Tex_Gen_Num_Section(data, self.offsets, self.mat_init_section)
        self.tex_coord_info_section = Tex_Coord_Info_Section(data, self.offsets, self.mat_init_section)
        self.tex_mtx_info_section = Tex_Mtx_Info_Section(data, self.offsets, self.mat_init_section)
        self.tex_no_section = Tex_No_Section(data, self.offsets, self.mat_init_section)
        self.font_no_section = Font_No_Section(data, self.offsets, self.mat_init_section)
        self.tev_order_info_section = Tev_Order_Info_Section(data, self.offsets, self.mat_init_section)
        self.tev_color_section = Tev_Color_Section(data, self.offsets, self.mat_init_section)
        self.tev_k_color_section = Tev_K_Color_Section(data, self.offsets, self.mat_init_section)
        self.tev_stage_num_section = Tev_Stage_Num_Section(data, self.offsets, self.mat_init_section)
        self.tev_stage_info_section = Tev_Stage_Info_Section(data, self.offsets, self.mat_init_section)
        self.tev_swap_mode_info_section = Tev_Swap_Mode_Info_Section(data, self.offsets, self.mat_init_section)
        self.tev_swap_mode_table_info_section = Tev_Swap_Mode_Table_Info_Section(data, self.offsets, self.mat_init_section)
        self.alpha_comp_info_section = Alpha_Comp_Info_Section(data, self.offsets, self.mat_init_section)
        self.blend_info_section = Blend_Info_Section(data, self.offsets, self.mat_init_section)
        self.dither_section = Dither_Section(data, self, self.offsets, self.mat_init_section)
        self.end_padding = b''
        self.last_block_size = int(0)

    def to_bytes(self):
        out = bytearray()
        out += self.magic
        out += self.size.to_bytes(4, 'big')
        out += self.material_count
        out += self.padding
        out += self.offsets.to_bytes()
        out += self.mat_init_section.to_bytes()
        out += self.mat_init_idx_section.to_bytes()
        out += self.mat_name_table_section.to_bytes()
        out += self.ind_init_data_section.to_bytes()
        out += self.cull_modes.to_bytes()
        out += self.mat_colors.to_bytes()
        out += self.color_chan_num_section.to_bytes()
        out += self.color_chan_info_section.to_bytes()
        out += self.tex_gen_num_section.to_bytes()
        out += self.tex_coord_info_section.to_bytes()
        out += self.tex_mtx_info_section.to_bytes()
        out += self.tex_no_section.to_bytes()
        out += self.font_no_section.to_bytes()
        out += self.tev_order_info_section.to_bytes()
        out += self.tev_color_section.to_bytes()
        out += self.tev_k_color_section.to_bytes()
        out += self.tev_stage_num_section.to_bytes()
        out += self.tev_stage_info_section.to_bytes()
        out += self.tev_swap_mode_info_section.to_bytes()
        out += self.tev_swap_mode_table_info_section.to_bytes()
        out += self.alpha_comp_info_section.to_bytes()
        out += self.blend_info_section.to_bytes()
        out += self.dither_section.to_bytes()
        out += self.end_padding
        return out

    @classmethod
    def from_json(cls, obj: dict, num_of_textures: int, tex1_section: TEX1_Section):
        self = cls.__new__(cls)

        self.magic = b'MAT1'
        self.material_count = len(obj["Materials"]).to_bytes(2, 'big')
        self.padding = b'\xFF'b'\xFF'

        # Initialize Offset Values
        self.offsets = MAT1_Section_Offsets.from_json(obj)

        # Init Data
        self.mat_init_section = Mat_Init_Data_Section.from_json(obj)

        # Init Data Idx
        self.mat_init_idx_section = Mat_Init_Data_Idx_Section.from_json(obj)

        # Mat Name Table
        self.mat_name_table_section = Mat_Name_Table_Section.from_json(obj)

        # Ind Init Data
        self.ind_init_data_section = Ind_Init_Data_Section.from_json(obj)

        # Cull Modes
        self.cull_modes = Cull_Mode_Section.from_json(obj)

        # Mat Colors
        self.mat_colors = Mat_Color_Section.from_json(obj)

        # Color Channel Numbers
        self.color_chan_num_section = Color_Chan_Num_Section.from_json(obj)

        # Color Channel Info
        self.color_chan_info_section = Color_Chan_Info_Section.from_json(obj)

        # Tex Gen Numbers
        self.tex_gen_num_section = Tex_Gen_Num_Section.from_json(obj)

        # Tex Coord Info
        self.tex_coord_info_section = Tex_Coord_Info_Section.from_json(obj)

        # Tex Mtx Info
        self.tex_mtx_info_section = Tex_Mtx_Info_Section.from_json(obj)

        # Tex No
        self.tex_no_section = Tex_No_Section.from_json(num_of_textures)

        # Font No
        self.font_no_section = Font_No_Section.from_json(obj)

        # Tev Order Info
        self.tev_order_info_section = Tev_Order_Info_Section.from_json(obj)

        # Tev Colors
        self.tev_color_section = Tev_Color_Section.from_json(obj)

        # TevK Colors
        self.tev_k_color_section = Tev_K_Color_Section.from_json(obj)

        # Tev Stage Numbers
        self.tev_stage_num_section = Tev_Stage_Num_Section.from_json(obj)

        # Tev Stage Info
        self.tev_stage_info_section = Tev_Stage_Info_Section.from_json(obj)

        # Tev Swap Mode Info
        self.tev_swap_mode_info_section = Tev_Swap_Mode_Info_Section.from_json(obj)

        # Tev Swap Mode Table Info
        self.tev_swap_mode_table_info_section = Tev_Swap_Mode_Table_Info_Section.from_json(obj)

        # Alpha Comp Info
        self.alpha_comp_info_section = Alpha_Comp_Info_Section.from_json(obj)

        # Blend Info
        self.blend_info_section = Blend_Info_Section.from_json(obj)

        # Dither
        self.dither_section = Dither_Section.from_json(obj)

        self.last_block_size = int(0)
        self.size = self.get_size_no_end_padding()
        self.end_padding = b''
        self.get_end_padding(self.size)
        self.size += len(self.end_padding)

        # Fill offset values
        self.get_offsets()

        # Fill Init Data Values
        self.get_init_data(obj, tex1_section)

        return self

    def get_size_no_end_padding(self):
        size = 12  # Header size
        size += self.offsets.size
        size += self.mat_init_section.size
        size += self.mat_init_idx_section.size
        size += self.mat_name_table_section.size

        # Get size of block after init data
        self.last_block_size += self.ind_init_data_section.size
        self.last_block_size += self.cull_modes.size
        self.last_block_size += self.mat_colors.size
        self.last_block_size += self.color_chan_num_section.size
        self.last_block_size += self.color_chan_info_section.size
        self.last_block_size += self.tex_gen_num_section.size
        self.last_block_size += self.tex_coord_info_section.size
        self.last_block_size += self.tex_mtx_info_section.size
        self.last_block_size += self.tex_no_section.size
        self.last_block_size += self.font_no_section.size
        self.last_block_size += self.tev_order_info_section.size
        self.last_block_size += self.tev_color_section.size
        self.last_block_size += self.tev_k_color_section.size
        self.last_block_size += self.tev_stage_num_section.size
        self.last_block_size += self.tev_stage_info_section.size
        self.last_block_size += self.tev_swap_mode_info_section.size
        self.last_block_size += self.tev_swap_mode_table_info_section.size
        self.last_block_size += self.alpha_comp_info_section.size
        self.last_block_size += self.blend_info_section.size
        self.last_block_size += self.dither_section.size
        size += self.last_block_size
        return size

    def get_end_padding(self, current_size: int):
        i = 0
        while current_size % 32 != 0:
            self.end_padding += padding_bytes[i]
            current_size += 1
            i += 1
        return current_size

    def get_offsets(self):
        first_offset = 12 + ((len(vars(self.offsets)) - 1) * 4)
        self.offsets.mat_init_data_offset = first_offset
        self.offsets.mat_init_data_indexes_offset = first_offset + self.mat_init_section.size
        self.offsets.mat_name_table_offset = self.offsets.mat_init_data_indexes_offset + self.mat_init_idx_section.size
        if self.ind_init_data_section.size > 0:
            self.offsets.ind_init_data_offset = self.offsets.mat_name_table_offset + self.mat_name_table_section.size
            self.offsets.cull_mode_offset = self.offsets.ind_init_data_offset + self.ind_init_data_section.size
        else:
            self.offsets.ind_init_data_offset = 0
            self.offsets.cull_mode_offset = self.offsets.mat_name_table_offset + self.mat_name_table_section.size
        self.offsets.mat_color_offset = self.offsets.cull_mode_offset + self.cull_modes.size
        self.offsets.color_chan_num_offset = self.offsets.mat_color_offset + self.mat_colors.size
        self.offsets.color_chan_info_offset = self.offsets.color_chan_num_offset + self.color_chan_num_section.size
        self.offsets.tex_gen_num_offset = self.offsets.color_chan_info_offset + self.color_chan_info_section.size
        self.offsets.tex_coord_info_offset = self.offsets.tex_gen_num_offset + self.tex_gen_num_section.size
        self.offsets.tex_mtx_info_offset = self.offsets.tex_coord_info_offset + self.tex_coord_info_section.size
        self.offsets.tex_no_offset = self.offsets.tex_mtx_info_offset + self.tex_mtx_info_section.size
        self.offsets.font_no_offset = self.offsets.tex_no_offset + self.tex_no_section.size
        self.offsets.tev_order_info_offset = self.offsets.font_no_offset + self.font_no_section.size
        self.offsets.tev_color_offset = self.offsets.tev_order_info_offset + self.tev_order_info_section.size
        self.offsets.tev_k_color_offset = self.offsets.tev_color_offset + self.tev_color_section.size
        self.offsets.tev_stage_num_offset = self.offsets.tev_k_color_offset + self.tev_k_color_section.size
        self.offsets.tev_stage_info_offset = self.offsets.tev_stage_num_offset + self.tev_stage_num_section.size
        self.offsets.tev_swap_mode_info_offset = self.offsets.tev_stage_info_offset + self.tev_stage_info_section.size
        self.offsets.tev_swap_mode_table_info_offset = self.offsets.tev_swap_mode_info_offset + self.tev_swap_mode_info_section.size
        self.offsets.alpha_comp_info_offset = self.offsets.tev_swap_mode_table_info_offset + self.tev_swap_mode_table_info_section.size
        self.offsets.blend_info_offset = self.offsets.alpha_comp_info_offset + self.alpha_comp_info_section.size
        self.offsets.dither_offset = self.offsets.blend_info_offset + self.blend_info_section.size

    def get_init_data(self, obj: dict, tex1_section: TEX1_Section):
        for i, material in enumerate(obj["Materials"]):
            initData = get_mat_init_data_entry(material.get("name"))
            self.mat_init_section.mat_init_data[i].mat_mode = initData.mat_mode

            # Cull Mode
            matching_cull_mode_idx = -1
            json_cull_mode_idx = bytes(str(material.get("cullmode")).encode())
            for cull_mode in self.cull_modes.cull_modes:
                if json_cull_mode_idx == cull_mode.to_bytes(1, 'big'):
                    break
                else:
                    matching_cull_mode_idx += 1
            self.mat_init_section.mat_init_data[i].cull_mode_idx = matching_cull_mode_idx.to_bytes(1, 'big')

            # Channel Num Idx
            matching_col_chan_idx = 0
            json_col_channel_num_idx = int(material.get("color_channel_count")).to_bytes(1, 'big')
            for col_chan_idx in self.color_chan_num_section.color_chan_num:
                if json_col_channel_num_idx == col_chan_idx:
                    break
                else:
                    matching_col_chan_idx += 1
            self.mat_init_section.mat_init_data[i].color_chan_num_idx = matching_col_chan_idx.to_bytes(1, 'big')

            # Tex Gen Num Idx
            matching_tex_gen_num_idx = 0
            json_tex_gen_num_idx = int(material.get("tex_gen_count")).to_bytes(1, 'big')
            for tex_gen_num_idx in self.tex_gen_num_section.tex_gen_num:
                if json_tex_gen_num_idx == tex_gen_num_idx:
                    break
                else:
                    matching_tex_gen_num_idx += 1
            self.mat_init_section.mat_init_data[i].tex_gen_num_idx = matching_tex_gen_num_idx.to_bytes(1, 'big')

            # Tev Stage Num Idx
            matching_tev_stage_num_idx = 0
            json_tev_stage_num_idx = int(material.get("tev_stage_count")).to_bytes(1, 'big')
            for tev_stage_num_idx in self.tev_stage_num_section.tev_stage_num:
                if json_tev_stage_num_idx == tev_stage_num_idx:
                    break
                else:
                    matching_tev_stage_num_idx += 1
            self.mat_init_section.mat_init_data[i].tev_stage_num_idx = matching_tev_stage_num_idx.to_bytes(1, 'big')

            # Dither Idx
            matching_dither_idx = 0
            json_dither_idx = int(material.get("dither")).to_bytes(1, 'big')
            for dither_idx in self.dither_section.dither:
                if json_dither_idx == dither_idx:
                    break
                else:
                    matching_dither_idx += 1
            self.mat_init_section.mat_init_data[i].dither_idx = matching_dither_idx.to_bytes(1, 'big')

            # Mat Alpha Calc
            self.mat_init_section.mat_init_data[i].mat_alpha_calc = initData.mat_alpha_calc

            # Unknown Field 7
            self.mat_init_section.mat_init_data[i].unknown_field_7 = initData.unknown_field_7

            # Mat Color Idx Table
            self.mat_init_section.mat_init_data[i].mat_color_idx_tbl = []
            for mat_color in material.get("matcolors", []):
                color_obj = GXColor.color_from_json_int(mat_color)
                for j, color in enumerate(self.mat_colors.colors):
                    if (color_obj.r == color.r and color_obj.g == color.g and
                            color_obj.b == color.b and color_obj.a == color.a):
                        self.mat_init_section.mat_init_data[i].mat_color_idx_tbl.append(j.to_bytes(2, 'big'))

            # Color Chan Info Idx Table
            self.mat_init_section.mat_init_data[i].color_chan_info_idx_tbl = initData.color_chan_info_idx_tbl

            # Tex Coord Info Idx Table
            self.mat_init_section.mat_init_data[i].tex_coord_info_idx_tbl = []
            for tex_coord_info in material.get("tex_coord_generators"):
                if tex_coord_info is None:
                    self.mat_init_section.mat_init_data[i].tex_coord_info_idx_tbl.append(b'\xFF'b'\xFF')
                    continue
                idx_str, data_str = tex_coord_info.split(";")
                idx = int(idx_str.split("=")[1])
                self.mat_init_section.mat_init_data[i].tex_coord_info_idx_tbl.append(idx.to_bytes(2, 'big'))

            # Tex Mtx Info Idx Table
            self.mat_init_section.mat_init_data[i].tex_mtx_info_idx_tbl = initData.tex_mtx_info_idx_tbl

            # Tex No Idx Table
            self.mat_init_section.mat_init_data[i].tex_no_idx_tbl = []
            for texture in material.get("textures"):
                if texture is None:
                    self.mat_init_section.mat_init_data[i].tex_no_idx_tbl.append(b'\xFF'b'\xFF')
                    continue

                for j, texture_ref in enumerate(tex1_section.texture_refs):
                    if texture_ref.texture == str(texture).encode():
                        self.mat_init_section.mat_init_data[i].tex_no_idx_tbl.append(j.to_bytes(2, 'big'))

            # Font No Idx
            if material.get("font") is None:
                self.mat_init_section.mat_init_data[i].font_no_idx = b'\xFF'b'\xFF'
            else:
                self.mat_init_section.mat_init_data[i].font_no_idx = bytes.fromhex(str(material.get("font")))

            # TevK Color Idx Table
            self.mat_init_section.mat_init_data[i].tev_k_color_idx_tbl = initData.tev_k_color_idx_tbl

            # TevK Color Selects
            self.mat_init_section.mat_init_data[i].tev_k_color_sel = []
            for tevk_color_select in material.get("tevkcolor_selects"):
                self.mat_init_section.mat_init_data[i].tev_k_color_sel.append(int(tevk_color_select).to_bytes(1, 'big'))

            # TevK Alpha Selects
            self.mat_init_section.mat_init_data[i].tev_k_alpha_sel = []
            for tevk_alpha_select in material.get("tevkalpha_selects"):
                self.mat_init_section.mat_init_data[i].tev_k_alpha_sel.append(int(tevk_alpha_select).to_bytes(1, 'big'))

            # Tev Order Info Idx Table
            self.mat_init_section.mat_init_data[i].tev_order_info_idx_tbl = []
            for tev_order in material.get("tevorders"):
                if tev_order is None:
                    self.mat_init_section.mat_init_data[i].tev_order_info_idx_tbl.append(b'\xFF'b'\xFF')
                    continue
                tev_order_obj = J2D_Tev_Order_Info.from_json(bytes.fromhex(tev_order))
                for j, tev_order_info in enumerate(self.tev_order_info_section.tev_order_info):
                    if (tev_order_obj.tex_coord == tev_order_info.tex_coord and tev_order_obj.tex_map == tev_order_info.tex_map and
                            tev_order_obj.color == tev_order_info.color and tev_order_obj.field_0x3 == tev_order_info.field_0x3):
                        self.mat_init_section.mat_init_data[i].tev_order_info_idx_tbl.append(j.to_bytes(2, 'big'))

            # Tev Color Idx Table
            self.mat_init_section.mat_init_data[i].tev_color_idx_tbl = []
            for tev_color in material.get("tevcolors"):
                if tev_color is None:
                    self.mat_init_section.mat_init_data[i].tev_color_idx_tbl.append(b'\xFF'b'\xFF')
                    continue
                color_obj = GXColorS10.from_json(bytes.fromhex(tev_color))
                for j, color in enumerate(self.tev_color_section.tev_color):
                    if (color_obj.r == color.r and color_obj.g == color.g and
                            color_obj.b == color.b and color_obj.a == color.a):
                        self.mat_init_section.mat_init_data[i].tev_color_idx_tbl.append(j.to_bytes(2, 'big'))

            # Tev Stage Info Idx Table
            self.mat_init_section.mat_init_data[i].tev_stage_info_idx_tbl = []
            for tev_stage_info in material.get("tevstages"):
                if tev_stage_info is None:
                    self.mat_init_section.mat_init_data[i].tev_stage_info_idx_tbl.append(b'\xFF'b'\xFF')
                    continue
                tev_stage_inf_obj = J2D_Tev_Stage_Info.from_json(bytes.fromhex(tev_stage_info))
                for j, tev_stage in enumerate(self.tev_stage_info_section.tev_stage_info):
                    if (tev_stage_inf_obj.field_0x0 == tev_stage.field_0x0 and tev_stage_inf_obj.color_a == tev_stage.color_a and
                            tev_stage_inf_obj.color_b == tev_stage.color_b and tev_stage_inf_obj.color_c == tev_stage.color_c and
                            tev_stage_inf_obj.color_d == tev_stage.color_d and tev_stage_inf_obj.c_op == tev_stage.c_op and
                            tev_stage_inf_obj.c_bias == tev_stage.c_bias and tev_stage_inf_obj.c_scale == tev_stage.c_scale and
                            tev_stage_inf_obj.c_clamp == tev_stage.c_clamp and tev_stage_inf_obj.c_reg == tev_stage.c_reg and
                            tev_stage_inf_obj.alpha_a == tev_stage.alpha_a and tev_stage_inf_obj.alpha_b == tev_stage.alpha_b and
                            tev_stage_inf_obj.alpha_c == tev_stage.alpha_c and tev_stage_inf_obj.alpha_d == tev_stage.alpha_d and
                            tev_stage_inf_obj.a_op == tev_stage.a_op and tev_stage_inf_obj.a_bias == tev_stage.a_bias and
                            tev_stage_inf_obj.a_scale == tev_stage.a_scale and tev_stage_inf_obj.a_clamp == tev_stage.a_clamp and
                            tev_stage_inf_obj.a_reg == tev_stage.a_reg and tev_stage_inf_obj.field_0x13 == tev_stage.field_0x13):
                        self.mat_init_section.mat_init_data[i].tev_stage_info_idx_tbl.append(j.to_bytes(2, 'big'))

            # Tev Swap Mode Info Idx Table
            self.mat_init_section.mat_init_data[i].tev_swap_mode_info_idx_tbl = []
            for tev_stage_swap_mode in material.get("tevstage_swapmodes"):
                if tev_stage_swap_mode is None:
                    self.mat_init_section.mat_init_data[i].tev_swap_mode_info_idx_tbl.append(b'\xFF'b'\xFF')
                    continue
                idx_str, data_str = tev_stage_swap_mode.split(";")
                idx = int(idx_str.split("=")[1])
                self.mat_init_section.mat_init_data[i].tev_swap_mode_info_idx_tbl.append(idx.to_bytes(2, 'big'))

            # Tev Swap Mode Table Info Idx Table
            self.mat_init_section.mat_init_data[i].tev_swap_mode_tbl_info_idx_tbl = initData.tev_swap_mode_tbl_info_idx_tbl

            # Alpha Comp Info Idx
            self.mat_init_section.mat_init_data[i].alpha_comp_info_idx = []
            for j, alpha_comp_info in enumerate(self.alpha_comp_info_section.alpha_comp_info):
                alpha_comp_info_obj = J2D_Alpha_Comp_Info.from_json(bytes.fromhex(material.get("alphacomp")))
                if (alpha_comp_info_obj.field_0x0 == alpha_comp_info.field_0x0 and alpha_comp_info_obj.field_0x1 == alpha_comp_info.field_0x1 and
                        alpha_comp_info_obj.ref_0 == alpha_comp_info.ref_0 and alpha_comp_info_obj.ref_1 == alpha_comp_info.ref_1 and
                        alpha_comp_info_obj.field_0x4 == alpha_comp_info.field_0x4 and alpha_comp_info_obj.field_0x5 == alpha_comp_info.field_0x5 and
                        alpha_comp_info_obj.field_0x6 == alpha_comp_info.field_0x6 and alpha_comp_info_obj.field_0x7 == alpha_comp_info.field_0x7):
                    self.mat_init_section.mat_init_data[i].alpha_comp_info_idx = j.to_bytes(2, 'big')

            # Blend Info Idx
            self.mat_init_section.mat_init_data[i].blend_info_idx = []
            for j, blend_info in enumerate(self.blend_info_section.blend_info):
                blend_obj = J2D_Blend_Info.from_json(bytes.fromhex(material.get("blend")))
                if (blend_obj.type == blend_info.type and blend_obj.src_factor == blend_info.src_factor and
                        blend_obj.dst_factor == blend_info.dst_factor and blend_obj.op == blend_info.op):
                    self.mat_init_section.mat_init_data[i].blend_info_idx = j.to_bytes(2, 'big')

            # Unknown field 0xE6
            self.mat_init_section.mat_init_data[i].unknown_field_E6 = initData.unknown_field_E6
    
    def adjust_mat1(self, mat_num: List[int]):
        # print(mat_num)
        size_change = int(0)
        size_change += self.mat_init_section.adjust_mat1_init_data(mat_num)
        self.mat_init_idx_section.adjust_mat_init_idx(mat_num)
        size_change += self.mat_name_table_section.adjust_table(mat_num)
        self.size -= size_change


class MAT1_Section_Offsets:
    def __init__(self, data: BytesIO):
        self.mat_init_data_offset = bytes_to_int(data.read(4))
        self.mat_init_data_indexes_offset = bytes_to_int(data.read(4))
        self.mat_name_table_offset = bytes_to_int(data.read(4))
        self.ind_init_data_offset = bytes_to_int(data.read(4))
        self.cull_mode_offset = bytes_to_int(data.read(4))
        self.mat_color_offset = bytes_to_int(data.read(4))
        self.color_chan_num_offset = bytes_to_int(data.read(4))
        self.color_chan_info_offset = bytes_to_int(data.read(4))
        self.tex_gen_num_offset = bytes_to_int(data.read(4))
        self.tex_coord_info_offset = bytes_to_int(data.read(4))
        self.tex_mtx_info_offset = bytes_to_int(data.read(4))
        self.tex_no_offset = bytes_to_int(data.read(4))
        self.font_no_offset = bytes_to_int(data.read(4))
        self.tev_order_info_offset = bytes_to_int(data.read(4))
        self.tev_color_offset = bytes_to_int(data.read(4))
        self.tev_k_color_offset = bytes_to_int(data.read(4))
        self.tev_stage_num_offset = bytes_to_int(data.read(4))
        self.tev_stage_info_offset = bytes_to_int(data.read(4))
        self.tev_swap_mode_info_offset = bytes_to_int(data.read(4))
        self.tev_swap_mode_table_info_offset = bytes_to_int(data.read(4))
        self.alpha_comp_info_offset = bytes_to_int(data.read(4))
        self.blend_info_offset = bytes_to_int(data.read(4))
        self.dither_offset = bytes_to_int(data.read(4))
        self.size = int(23 * 4)

    def find_next_valid_offset(self, current_offset: int):
        offsets = [
            self.mat_init_data_offset,
            self.mat_init_data_indexes_offset,
            self.mat_name_table_offset,
            self.ind_init_data_offset,
            self.cull_mode_offset,
            self.mat_color_offset,
            self.color_chan_num_offset,
            self.color_chan_info_offset,
            self.tex_gen_num_offset,
            self.tex_coord_info_offset,
            self.tex_mtx_info_offset,
            self.tex_no_offset,
            self.font_no_offset,
            self.tev_order_info_offset,
            self.tev_color_offset,
            self.tev_k_color_offset,
            self.tev_stage_num_offset,
            self.tev_stage_info_offset,
            self.tev_swap_mode_info_offset,
            self.tev_swap_mode_table_info_offset,
            self.alpha_comp_info_offset,
            self.blend_info_offset,
            self.dither_offset,
        ]

        candidates = [
            o for o in offsets
            if o != 0 and o > current_offset
        ]

        if not candidates:
            return None

        return min(candidates)

    def to_bytes(self):
        out = bytearray()
        out += self.mat_init_data_offset.to_bytes(4, 'big')
        out += self.mat_init_data_indexes_offset.to_bytes(4, 'big')
        out += self.mat_name_table_offset.to_bytes(4, 'big')
        out += self.ind_init_data_offset.to_bytes(4, 'big')
        out += self.cull_mode_offset.to_bytes(4, 'big')
        out += self.mat_color_offset.to_bytes(4, 'big')
        out += self.color_chan_num_offset.to_bytes(4, 'big')
        out += self.color_chan_info_offset.to_bytes(4, 'big')
        out += self.tex_gen_num_offset.to_bytes(4, 'big')
        out += self.tex_coord_info_offset.to_bytes(4, 'big')
        out += self.tex_mtx_info_offset.to_bytes(4, 'big')
        out += self.tex_no_offset.to_bytes(4, 'big')
        out += self.font_no_offset.to_bytes(4, 'big')
        out += self.tev_order_info_offset.to_bytes(4, 'big')
        out += self.tev_color_offset.to_bytes(4, 'big')
        out += self.tev_k_color_offset.to_bytes(4, 'big')
        out += self.tev_stage_num_offset.to_bytes(4, 'big')
        out += self.tev_stage_info_offset.to_bytes(4, 'big')
        out += self.tev_swap_mode_info_offset.to_bytes(4, 'big')
        out += self.tev_swap_mode_table_info_offset.to_bytes(4, 'big')
        out += self.alpha_comp_info_offset.to_bytes(4, 'big')
        out += self.blend_info_offset.to_bytes(4, 'big')
        out += self.dither_offset.to_bytes(4, 'big')
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        self.mat_init_data_offset = int(0)
        self.mat_init_data_indexes_offset = int(0)
        self.mat_name_table_offset = int(0)
        self.ind_init_data_offset = int(0)
        self.cull_mode_offset = int(0)
        self.mat_color_offset = int(0)
        self.color_chan_num_offset = int(0)
        self.color_chan_info_offset = int(0)
        self.tex_gen_num_offset = int(0)
        self.tex_coord_info_offset = int(0)
        self.tex_mtx_info_offset = int(0)
        self.tex_no_offset = int(0)
        self.font_no_offset = int(0)
        self.tev_order_info_offset = int(0)
        self.tev_color_offset = int(0)
        self.tev_k_color_offset = int(0)
        self.tev_stage_num_offset = int(0)
        self.tev_stage_info_offset = int(0)
        self.tev_swap_mode_info_offset = int(0)
        self.tev_swap_mode_table_info_offset = int(0)
        self.alpha_comp_info_offset = int(0)
        self.blend_info_offset = int(0)
        self.dither_offset = int(0)

        self.size = int(23 * 4)

        return self


# Mat Init Data ------------------------
class Mat_Init_Data_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets):
        self.size = int(0)
        blockCount = (offsets.mat_init_data_indexes_offset - offsets.mat_init_data_offset) // 232
        self.mat_init_data: List[J2D_Material_Init_Data] = []
        for i in range(blockCount):
            initData = J2D_Material_Init_Data(data)
            self.mat_init_data.insert(i, initData)
        self.padding = data.read((offsets.mat_init_data_indexes_offset - offsets.mat_init_data_offset) - (blockCount * 232))
        self.size = (len(self.mat_init_data) * 232) + len(self.padding)

        # Get number of values for each data section
        self.cull_mode_count = int(0)
        self.mat_color_count = int(0)
        self.color_chan_num_count = int(0)
        self.color_chan_info_count = int(0)
        self.tex_gen_num_count = int(0)
        self.tex_coord_info_count = int(0)
        self.tex_mtx_info_count = int(0)
        self.tex_no_count = int(0)
        self.font_no_count = int(0)
        self.tev_order_info_count = int(0)
        self.tev_color_count = int(0)
        self.tev_k_color_count = int(0)
        self.tev_stage_num_count = int(0)
        self.tev_stage_info_count = int(0)
        self.tev_swap_mode_info_count = int(0)
        self.tev_swap_mode_table_info_count = int(0)
        self.alpha_comp_info_count = int(0)
        self.blend_info_count = int(0)
        self.dither_count = int(0)
        for i, matInitData in enumerate(self.mat_init_data):
            # Cull Modes
            cullModeCount = bytes_to_int(self.mat_init_data[i].cull_mode_idx)
            if cullModeCount > self.cull_mode_count:
                self.cull_mode_count = cullModeCount

            # Mat Colors
            for j in range(1):
                matColorCount = bytes_to_int(self.mat_init_data[i].mat_color_idx_tbl[j])
                if matColorCount > self.mat_color_count:
                    self.mat_color_count = matColorCount

            # Color Chan Nums
            colorChanNumCount = bytes_to_int(self.mat_init_data[i].color_chan_num_idx)
            if colorChanNumCount > self.color_chan_num_count:
                self.color_chan_num_count = colorChanNumCount

            # Color Chan Info
            for j in range(3):
                colorChanInfoCount = bytes_to_int(self.mat_init_data[i].color_chan_info_idx_tbl[j])
                if colorChanInfoCount > self.color_chan_info_count:
                    self.color_chan_info_count = colorChanInfoCount

            # Tex Gen Num
            texGenNumCount = bytes_to_int(self.mat_init_data[i].tex_gen_num_idx)
            if texGenNumCount > self.tex_gen_num_count:
                self.tex_gen_num_count = texGenNumCount

            # Tex Coord Info
            for j in range(7):
                texCoordInfoCount = bytes_to_int(self.mat_init_data[i].tex_coord_info_idx_tbl[j])
                if texCoordInfoCount > self.tex_coord_info_count:
                    self.tex_coord_info_count = texCoordInfoCount

            # Tex Mtx Info
            for j in range(9):
                texMtxInfoCount = bytes_to_int(self.mat_init_data[i].tex_mtx_info_idx_tbl[j])
                if texMtxInfoCount > self.tex_mtx_info_count:
                    self.tex_mtx_info_count = texMtxInfoCount

            # Tex No
            for j in range(7):
                texNoCount = bytes_to_int(self.mat_init_data[i].tex_no_idx_tbl[j])
                if texNoCount > self.tex_no_count:
                    self.tex_no_count = texNoCount

            # Font No
            fontNoCount = bytes_to_int(self.mat_init_data[i].font_no_idx)
            if fontNoCount > self.font_no_count:
                self.font_no_count = fontNoCount

            # Tev Order Info
            for j in range(15):
                tevOrderInfoCount = bytes_to_int(self.mat_init_data[i].tev_order_info_idx_tbl[j])
                if tevOrderInfoCount > self.tev_order_info_count:
                    self.tev_order_info_count = tevOrderInfoCount

            # Tev Color
            for j in range(3):
                tevColorCount = bytes_to_int(self.mat_init_data[i].tev_color_idx_tbl[j])
                if tevColorCount > self.tev_color_count:
                    self.tev_color_count = tevColorCount

            # TevK Color
            for j in range(3):
                tevKColorCount = bytes_to_int(self.mat_init_data[i].tev_k_color_idx_tbl[j])
                if tevKColorCount > self.tev_k_color_count:
                    self.tev_k_color_count = tevKColorCount

            # Tev Stage Num
            tevStageNumCount = bytes_to_int(self.mat_init_data[i].tev_stage_num_idx)
            if tevStageNumCount > self.tev_stage_num_count:
                self.tev_stage_num_count = tevStageNumCount

            # Tev Stage Info
            for j in range(15):
                tevStageInfoCount = bytes_to_int(self.mat_init_data[i].tev_stage_info_idx_tbl[j])
                if tevStageInfoCount > self.tev_stage_info_count:
                    self.tev_stage_info_count = tevStageInfoCount

            # Tev Swap Mode Info
            for j in range(15):
                tevSwapModeInfoCount = bytes_to_int(self.mat_init_data[i].tev_swap_mode_info_idx_tbl[j])
                if tevSwapModeInfoCount > self.tev_swap_mode_info_count:
                    self.tev_swap_mode_info_count = tevSwapModeInfoCount

            # Tev Swap Mode Table Info
            for j in range(3):
                tevSwapModeTableInfoCount = bytes_to_int(self.mat_init_data[i].tev_swap_mode_tbl_info_idx_tbl[j])
                if tevSwapModeTableInfoCount > self.tev_swap_mode_table_info_count:
                    self.tev_swap_mode_table_info_count = tevSwapModeTableInfoCount

            # Alpha Comp Info
            alphaCompInfoCount = bytes_to_int(self.mat_init_data[i].alpha_comp_info_idx)
            if alphaCompInfoCount > self.alpha_comp_info_count:
                self.alpha_comp_info_count = alphaCompInfoCount

            # Blend Info
            blendInfoCount = bytes_to_int(self.mat_init_data[i].blend_info_idx)
            if blendInfoCount > self.blend_info_count:
                self.blend_info_count = blendInfoCount

            # Dither
            ditherCount = bytes_to_int(self.mat_init_data[i].dither_idx)
            if ditherCount > self.dither_count:
                self.dither_count = ditherCount
        self.cull_mode_count += 1
        self.mat_color_count += 1
        self.color_chan_num_count += 1
        self.color_chan_info_count += 1
        self.tex_gen_num_count += 1
        self.tex_coord_info_count += 1
        self.tex_mtx_info_count += 1
        self.tex_no_count += 1
        self.font_no_count += 1
        self.tev_order_info_count += 1
        self.tev_color_count += 1
        self.tev_k_color_count += 1
        self.tev_stage_num_count += 1
        self.tev_stage_info_count += 1
        self.tev_swap_mode_info_count += 1
        self.tev_swap_mode_table_info_count += 1
        self.alpha_comp_info_count += 1
        self.blend_info_count += 1
        self.dither_count += 1

    def to_bytes(self):
        out = bytearray()
        for initData in self.mat_init_data:
            out += initData.mat_mode
            out += initData.cull_mode_idx
            out += initData.color_chan_num_idx
            out += initData.tex_gen_num_idx
            out += initData.tev_stage_num_idx
            out += initData.dither_idx
            out += initData.mat_alpha_calc
            out += initData.unknown_field_7
            for matColorIdx in initData.mat_color_idx_tbl:
                out += matColorIdx
            for colorChanInfoIdx in initData.color_chan_info_idx_tbl:
                out += colorChanInfoIdx
            for texCoordInfoIdx in initData.tex_coord_info_idx_tbl:
                out += texCoordInfoIdx
            for texMtxInfoIdx in initData.tex_mtx_info_idx_tbl:
                out += texMtxInfoIdx
            for texNoIdx in initData.tex_no_idx_tbl:
                out += texNoIdx
            out += initData.font_no_idx
            for tevKColorIdx in initData.tev_k_color_idx_tbl:
                out += tevKColorIdx
            for tevKColorSel in initData.tev_k_color_sel:
                out += tevKColorSel
            for tevKAlphaSel in initData.tev_k_alpha_sel:
                out += tevKAlphaSel
            for tevOrderInfoIdx in initData.tev_order_info_idx_tbl:
                out += tevOrderInfoIdx
            for tevColorIdx in initData.tev_color_idx_tbl:
                out += tevColorIdx
            for tevStageInfoIdx in initData.tev_stage_info_idx_tbl:
                out += tevStageInfoIdx
            for tevSwapModeInfoIdx in initData.tev_swap_mode_info_idx_tbl:
                out += tevSwapModeInfoIdx
            for tevSwapModeTblInfoIdx in initData.tev_swap_mode_tbl_info_idx_tbl:
                out += tevSwapModeTblInfoIdx
            out += initData.alpha_comp_info_idx
            out += initData.blend_info_idx
            out += initData.unknown_field_E6
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        self.mat_init_data = []
        for i in range(len(obj["Materials"])):
            self.mat_init_data.insert(i, J2D_Material_Init_Data.from_json(obj))

        size = len(obj["Materials"]) * 232
        self.padding = b''
        i = 0
        while size % 4 != 0:
            size += 1
            self.padding += padding_bytes[i]
            i += 1

        self.size = (len(self.mat_init_data) * 232) + len(self.padding)

        return self
    
    def adjust_mat1_init_data(self, indexes: List[int]):
        init_data_indexes = set(indexes)
        mat_init_data_to_keep: List[J2D_Material_Init_Data] = []
        for i, init_data in enumerate(self.mat_init_data):
            if i in init_data_indexes:
                # print(f"Deleting init data: {i}")
                continue
            else:
                mat_init_data_to_keep.append(init_data)
        self.mat_init_data = mat_init_data_to_keep
        size_change = int(232) * len(indexes)
        self.size -= size_change
        return size_change


class J2D_Material_Init_Data:
    def __init__(self, data: BytesIO):
        self.mat_mode = data.read(1)
        self.cull_mode_idx = data.read(1)
        self.color_chan_num_idx = data.read(1)
        self.tex_gen_num_idx = data.read(1)
        self.tev_stage_num_idx = data.read(1)
        self.dither_idx = data.read(1)
        self.mat_alpha_calc = data.read(1)
        self.unknown_field_7 = data.read(1)
        self.mat_color_idx_tbl: List[bytes] = [data.read(2), data.read(2)]
        self.color_chan_info_idx_tbl: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2)]
        self.tex_coord_info_idx_tbl: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2)]
        self.tex_mtx_info_idx_tbl: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2)]
        self.tex_no_idx_tbl: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2)]
        self.font_no_idx = data.read(2)
        self.tev_k_color_idx_tbl: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2)]
        self.tev_k_color_sel: List[bytes] = [data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1),
                                             data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1)]
        self.tev_k_alpha_sel: List[bytes] = [data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1),
                                             data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1), data.read(1)]
        self.tev_order_info_idx_tbl: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2),
                                                    data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2)]
        self.tev_color_idx_tbl: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2)]
        self.tev_stage_info_idx_tbl: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2),
                                                    data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2)]
        self.tev_swap_mode_info_idx_tbl: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2),
                                                        data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2)]
        self.tev_swap_mode_tbl_info_idx_tbl: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2)]
        self.alpha_comp_info_idx = data.read(2)
        self.blend_info_idx = data.read(2)
        self.unknown_field_E6 = data.read(2)

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Initialize
        self.mat_mode = b'\x00'
        self.cull_mode_idx = b'\x00'
        self.color_chan_num_idx = b'\x00'
        self.tex_gen_num_idx = b'\x00'
        self.tev_stage_num_idx = b'\x00'
        self.dither_idx = b'\x00'
        self.mat_alpha_calc = b'\x00'
        self.unknown_field_7 = b'\x00'
        self.mat_color_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00']
        self.color_chan_info_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00']
        self.tex_coord_info_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00']
        self.tex_mtx_info_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00']
        self.tex_no_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00']
        self.font_no_idx = b'\x00'b'\x00'
        self.tev_k_color_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00']
        self.tev_k_color_sel = [b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00',
                                b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00']
        self.tev_k_alpha_sel = [b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00',
                                b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00', b'\x00']
        self.tev_order_info_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00',
                                       b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00']
        self.tev_color_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00']
        self.tev_stage_info_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00',
                                       b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00']
        self.tev_swap_mode_info_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00',
                                           b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00']
        self.tev_swap_mode_tbl_info_idx_tbl = [b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00', b'\x00'b'\x00']
        self.alpha_comp_info_idx = b'\x00'b'\x00'
        self.blend_info_idx = b'\x00'b'\x00'
        self.unknown_field_E6 = b'\x00'b'\x00'

        return self

    @classmethod
    def new_entry(cls, data):
        self = cls.__new__(cls)

        initData = get_mat_init_data_entry(data["parent_mat_name"])

        if data["edit_material"] == "No":
            return initData

        return self
# --------------------------------------


# Mat Init Data Idx ---------------------
class Mat_Init_Data_Idx_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets):
        self.size = int(0)
        if offsets.mat_init_data_indexes_offset > 0:
            indexCount = (offsets.mat_init_data_indexes_offset - offsets.mat_init_data_offset) // 232
            self.indexes: List[bytes] = []
            for i in range(indexCount):
                index = data.read(2)
                self.indexes.insert(i, index)
                self.size += 2

            self.padding = b''
            while self.size % 4 != 0:
                self.padding += data.read(1)
                self.size += 1

    def to_bytes(self):
        out = bytearray()
        for index in self.indexes:
            out += index
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Calculate Init Data Indexes
        self.indexes = []
        for i in range(len(obj["Materials"])):
            self.indexes.insert(i, i.to_bytes(2, 'big'))

        # Calculate Padding
        self.size = len(self.indexes) * 2
        self.padding = b''
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            self.padding += padding_bytes[i]
            i += 1

        return self
    
    def adjust_mat_init_idx(self, mat_nums: List[int]):
        indexes_set = set(mat_nums)
        indexes_to_keep: List[bytes] = []
        for i, idx in enumerate(self.indexes):
            if i in indexes_set:
                continue
            else:
                indexes_to_keep.append(idx)
        self.indexes = indexes_to_keep
        for i in range(len(self.indexes)):
            self.indexes[i] = i.to_bytes(2, 'big')
            
        size_change = int(0)
        for idx in mat_nums:
            size_change += 2
        
        self.size -= size_change

        i = 0
        self.padding = b''
        while self.size % 8 != 0:
            self.size += 1
            self.padding += padding_bytes[i]
            i += 1
        print(f"mat init idx size: {self.size}")
# --------------------------------------


# Mat Name Table -----------------------
class Mat_Name_Table_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, init_indexes: Mat_Init_Data_Idx_Section):
        self.size = 0
        self.num_of_entries = data.read(2)
        self.start_padding = data.read(2)

        self.header_entries: List[Mat_Name_Table_Header_Entry] = []
        for i in range(bytes_to_int(self.num_of_entries)):
            self.header_entries.insert(i, Mat_Name_Table_Header_Entry(data))

        self.mat_names: List[bytes] = []
        for i, entries in enumerate(self.header_entries):
            if i + 1 < len(self.header_entries):
                self.mat_names.insert(i,
                                      data.read(bytes_to_int(self.header_entries[i + 1].mat_name_offset) -
                                                bytes_to_int(self.header_entries[i].mat_name_offset)))
            else:
                self.mat_names.insert(i, b'')
                while True:
                    self.mat_names[i] += data.read(1)
                    if b'\x00' in self.mat_names[i]:
                        break

        # Calculate Padding
        size = int(0)
        for i, entry in enumerate(self.header_entries):
            size += len(entry.id)
            size += len(entry.mat_name_offset)
            size += len(self.mat_names[i])
        self.padding = b''
        while size % 4 != 0:
            size += 1
            self.padding += data.read(1)

        # Calculate Size
        self.size = 4 + (bytes_to_int(self.num_of_entries) * 4)
        for name in self.mat_names:
            self.size += len(name)
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        out += self.num_of_entries
        out += self.start_padding
        for i, entry in enumerate(self.header_entries):
            out += entry.to_bytes()
        for i, entry in enumerate(self.header_entries):
            out += self.mat_names[i]
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        self.num_of_entries = len(obj["Materials"]).to_bytes(2, 'big')
        self.start_padding = b'\xFF'b'\xFF'

        offset = int(4)

        self.header_entries = []
        for i in range(bytes_to_int(self.num_of_entries)):
            self.header_entries.insert(i, Mat_Name_Table_Header_Entry.from_json(str(obj["Materials"][i]["name"])))
            offset += 4

        self.mat_names = []
        for i in range(bytes_to_int(self.num_of_entries)):
            self.mat_names.insert(i, str(obj["Materials"][i]["name"]).encode())
            self.mat_names[i] += b'\x00'
            self.header_entries[i].mat_name_offset = offset.to_bytes(2, 'big')
            offset += len(self.mat_names[i])

        self.calc_padding()

        return self

    def calc_padding(self):
        self.size = 4 + (bytes_to_int(self.num_of_entries) * 4)
        for name in self.mat_names:
            self.size += len(name)
        self.padding = b''
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            self.padding += padding_bytes[i]
            i += 1

    def new_entry(self, mat_name: str):
        self.header_entries.append(Mat_Name_Table_Header_Entry.add_entry(mat_name))
        self.num_of_entries = (bytes_to_int(self.num_of_entries) + 1).to_bytes(2, 'big')
        self.mat_names.append(mat_name.encode())
        self.mat_names[len(self.mat_names) - 1] += b'\x00'
        self.header_entries[len(self.header_entries) - 1].mat_name_offset = (bytes_to_int(self.header_entries[len(self.header_entries) - 2].mat_name_offset) +
                                                                             len(self.mat_names[len(self.mat_names) - 1])).to_bytes(2, 'big')
        size_change = 4 + len(self.mat_names[len(self.mat_names) - 1])
        self.size += size_change
        return size_change
    
    def adjust_table(self, mat_nums: List[int]) -> int:
        mat_nums_set = set(mat_nums)
        header_entries_to_keep: List[Mat_Name_Table_Header_Entry] = []
        mat_names_to_keep: List[bytes] = []
        removed_names: List[bytes] = []
        for i, (header, name) in enumerate(zip(self.header_entries, self.mat_names)):
            if i in mat_nums_set:
                print(f"Deleting header: {i}")
                print(f"Deleting mat name: {i}")
                removed_names.append(name)
            else:
                header_entries_to_keep.append(header)
                mat_names_to_keep.append(name)
        self.header_entries = header_entries_to_keep
        self.mat_names = mat_names_to_keep
        self.num_of_entries = len(self.header_entries).to_bytes(2, 'big')

        offset = int(4)
        for i in range(bytes_to_int(self.num_of_entries)):
            offset += 4
        for i in range(bytes_to_int(self.num_of_entries)):
            self.header_entries[i].mat_name_offset = offset.to_bytes(2, 'big')
            offset += len(self.mat_names[i])
        
        size_change = int(4)
        for removed_name in removed_names:
            size_change += len(removed_name)
            size_change += 4  # For header entries deleted
        self.calc_padding()
        orig_pad_size = len(self.padding)
        self.size += orig_pad_size - len(self.padding)
        size_change += orig_pad_size - len(self.padding)
        # print(f"mat name table size: {self.size}")
        return size_change

class Mat_Name_Table_Header_Entry:
    def __init__(self, data: BytesIO):
        self.id = data.read(2)
        self.mat_name_offset = data.read(2)

    def to_bytes(self):
        out = bytearray()
        out += self.id
        out += self.mat_name_offset
        return out

    @classmethod
    def from_json(cls, mat_name: str):
        self = cls.__new__(cls)

        self.id = calc_keycode(mat_name).to_bytes(2, 'big')
        self.mat_name_offset = b'\x00'b'\x00'

        return self

    @classmethod
    def add_entry(cls, mat_name: str):
        self = cls.__new__(cls)

        self.id = calc_keycode(mat_name).to_bytes(2, 'big')
        self.mat_name_offset = b'\x00'b'\x00'

        return self

# --------------------------------------


# Ind Init Data ------------------------
class Ind_Init_Data_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets):
        self.size = 0
        self.data = bytes()
        if offsets.ind_init_data_offset > 0:
            self.data = data.read(offsets.cull_mode_offset - offsets.ind_init_data_offset)

    def to_bytes(self):
        out = bytearray()
        out += self.data
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        for material in obj["Materials"]:
            ind_data = material.get("indirectdata")
            if ind_data:
                self.data = bytes(ind_data)
            else:
                self.data = b''

        self.size = len(self.data)

        return self
# --------------------------------------


# Cull Modes ---------------------------
class Cull_Mode_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.cull_mode_offset > 0:
            self.cull_modes: List[int] = []
            for i in range(initData.cull_mode_count):
                self.cull_modes.insert(i, bytes_to_int(data.read(4)))
            next_offset = offsets.find_next_valid_offset(offsets.cull_mode_offset)
            self.padding = data.read((next_offset - offsets.cull_mode_offset) - ((initData.cull_mode_count) * 4))
        # Calculate size
        self.size = len(self.cull_modes) * 4
        i = 0
        while self.size % 2 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for cullMode in self.cull_modes:
            out += cullMode.to_bytes(4, 'big')
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        self.cull_modes = [2, 1, 0]

        self.padding = b''
        self.size = len(self.cull_modes) * 4
        i = 0
        while self.size % 2 != 0:
            self.size += 1
            self.padding += padding_bytes[i]
            i += 1

        return self
# --------------------------------------


# Mat Colors ---------------------------
class Mat_Color_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.mat_color_offset > 0:
            self.colors: List[GXColor] = []
            for i in range(initData.mat_color_count):
                self.colors.insert(i, GXColor(data))
            next_offset = offsets.find_next_valid_offset(offsets.mat_color_offset)
            self.padding = data.read((next_offset - offsets.mat_color_offset) - ((initData.mat_color_count) * 4))
        # Calculate size
        self.size = len(self.colors) * 4
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for color in self.colors:
            out += color.r
            out += color.g
            out += color.b
            out += color.a
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Gather every *unique* GXColor data instance
        self.colors = []
        idx = int(0)
        seen = set()
        for material in obj["Materials"]:
            for mat_color in material.get("matcolors", []):
                color_obj = GXColor.color_from_json_int(mat_color)
                key = (color_obj.r, color_obj.g, color_obj.b, color_obj.a)
                if key not in seen:
                    self.colors.insert(idx, color_obj)
                    seen.add(key)
                    idx += 1

        # Calculate Padding
        self.size = len(self.colors) * 4
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self


class GXColor:
    def __init__(self, data: BytesIO):
        self.r = data.read(1)
        self.g = data.read(1)
        self.b = data.read(1)
        self.a = data.read(1)

    @classmethod
    def color_from_json_int(cls, color: list[int]):
        self = cls.__new__(cls)

        self.r = color[0].to_bytes(1, 'big')
        self.g = color[1].to_bytes(1, 'big')
        self.b = color[2].to_bytes(1, 'big')
        self.a = color[3].to_bytes(1, 'big')

        return self

    @classmethod
    def from_json(cls, data: bytes):
        self = cls.__new__(cls)

        self.r = data[0].to_bytes(1, 'big')
        self.g = data[1].to_bytes(1, 'big')
        self.b = data[2].to_bytes(1, 'big')
        self.a = data[3].to_bytes(1, 'big')

        return self
# --------------------------------------


# Color Chan Nums ----------------------
class Color_Chan_Num_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.color_chan_num_offset > 0:
            self.color_chan_num: List[bytes] = []
            for i in range(initData.color_chan_num_count):
                self.color_chan_num.insert(i, data.read(1))
            next_offset = offsets.find_next_valid_offset(offsets.color_chan_num_offset)
            self.padding = data.read((next_offset - offsets.color_chan_num_offset) - ((initData.color_chan_num_count)))
        # Calculate size
        self.size = len(self.color_chan_num)
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for colorChanNum in self.color_chan_num:
            out += colorChanNum
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Get highest channel num
        highest_channel_num = int(0)
        for material in obj["Materials"]:
            if int(material["color_channel_count"]) > highest_channel_num:
                highest_channel_num = int(material["color_channel_count"])

        self.color_chan_num = []
        for i in range(highest_channel_num):
            self.color_chan_num.insert(i, (i + 1).to_bytes(1, 'big'))

        # Calculate Padding
        self.size = len(self.color_chan_num)
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self
# --------------------------------------


# Color Chan Info ----------------------
class Color_Chan_Info_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.color_chan_info_offset > 0:
            self.color_chan_info: List[J2D_Color_Chan_Info] = []
            for i in range(initData.color_chan_info_count):
                self.color_chan_info.insert(i, J2D_Color_Chan_Info(data))
            next_offset = offsets.find_next_valid_offset(offsets.color_chan_info_offset)
            self.padding = data.read((next_offset - offsets.color_chan_info_offset) - ((initData.color_chan_info_count) * 4))
        # Calculate size
        self.size = len(self.color_chan_info) * 4
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for colorChanInfo in self.color_chan_info:
            out += colorChanInfo.field_0x0
            out += colorChanInfo.field_0x1
            out += colorChanInfo.field_0x2
            out += colorChanInfo.field_0x3
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Gather every *unique* J2DColorChanInfo data instance
        unique_values: List[J2D_Color_Chan_Info] = []
        idx = int(0)
        seen = set()
        for material in obj["Materials"]:
            for colorChanInfo in material.get("color_channels", []):
                colorChanInfoObj = J2D_Color_Chan_Info.from_json(bytes.fromhex(colorChanInfo))
                key = (colorChanInfoObj.field_0x0, colorChanInfoObj.field_0x1, colorChanInfoObj.field_0x2, colorChanInfoObj.field_0x3)
                if key not in seen:
                    unique_values.insert(idx, colorChanInfoObj)
                    seen.add(key)
                    idx += 1

        self.color_chan_info = []
        for i in range(len(unique_values) + 1):
            if i < len(unique_values):
                if (unique_values[i].field_0x0 == b'\x00' and unique_values[i].field_0x1 == b'\x01' and
                        unique_values[i].field_0x2 == b'\xff' and unique_values[i].field_0x3 == b'\xff'):
                    self.color_chan_info.insert(i, unique_values[i])
                    self.color_chan_info.insert(i + 1, unique_values[i])
                    i += 1
                else:
                    self.color_chan_info.insert(i, unique_values[i])

        # Calculate Padding
        self.size = len(self.color_chan_info) * 4
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self


class J2D_Color_Chan_Info:
    def __init__(self, data: BytesIO):
        self.field_0x0 = data.read(1)
        self.field_0x1 = data.read(1)
        self.field_0x2 = data.read(1)
        self.field_0x3 = data.read(1)

    @classmethod
    def from_json(cls, data: bytes):
        self = cls.__new__(cls)

        self.field_0x0 = data[0].to_bytes(1, 'big')
        self.field_0x1 = data[1].to_bytes(1, 'big')
        self.field_0x2 = data[2].to_bytes(1, 'big')
        self.field_0x3 = data[3].to_bytes(1, 'big')

        return self
# --------------------------------------


# Tex Gen Num --------------------------
class Tex_Gen_Num_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.tex_gen_num_offset > 0:
            self.tex_gen_num: List[bytes] = []
            for i in range(initData.tex_gen_num_count):
                self.tex_gen_num.insert(i, data.read(1))
            next_offset = offsets.find_next_valid_offset(offsets.tex_gen_num_offset)
            self.padding = data.read((next_offset - offsets.tex_gen_num_offset) - (initData.tex_gen_num_count))
        # Calculate size
        self.size = len(self.tex_gen_num)
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for texGenNum in self.tex_gen_num:
            out += texGenNum
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Get highest tex gen num
        highest_tex_gen_num = int(0)
        for material in obj["Materials"]:
            if int(material["tex_gen_count"]) > highest_tex_gen_num:
                highest_tex_gen_num = int(material["tex_gen_count"])

        self.tex_gen_num = []
        for i in range(highest_tex_gen_num):
            self.tex_gen_num.insert(i, (i + 1).to_bytes(1, 'big'))

        # Calculate Padding
        self.size = len(self.tex_gen_num)
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self
# --------------------------------------


# Tex Coord Info -----------------------
class Tex_Coord_Info_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.tex_coord_info_offset > 0:
            self.tex_coord_info: List[J2D_Tex_Coord_Info] = []
            for i in range(initData.tex_coord_info_count):
                self.tex_coord_info.insert(i, J2D_Tex_Coord_Info(data))
            next_offset = offsets.find_next_valid_offset(offsets.tex_coord_info_offset)
            self.padding = data.read((next_offset - offsets.tex_coord_info_offset) - ((initData.tex_coord_info_count) * 4))
        # Calculate size
        self.size = len(self.tex_coord_info) * 4
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for texCoordInfo in self.tex_coord_info:
            out += texCoordInfo.tex_gen_type
            out += texCoordInfo.tex_gen_src
            out += texCoordInfo.tex_gen_mtx
            out += texCoordInfo.padding
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        unique_bytes = []
        highest_idx = -1

        for material in obj["Materials"]:
            for entry in material["tex_coord_generators"]:
                if entry is None:
                    continue

                idx_str, data_str = entry.split(";")
                idx = int(idx_str.split("=")[1])
                data = bytes.fromhex(data_str)

                if idx > highest_idx:
                    highest_idx = idx

                if data not in unique_bytes:
                    unique_bytes.append(data)

        self.tex_coord_info = []
        for i in range(highest_idx + 1):
            self.tex_coord_info.insert(i, J2D_Tex_Coord_Info.from_json(unique_bytes[i]))

        # Calculate Padding
        self.size = len(self.tex_coord_info) * 4
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self


class J2D_Tex_Coord_Info:
    def __init__(self, data: BytesIO):
        self.tex_gen_type = data.read(1)
        self.tex_gen_src = data.read(1)
        self.tex_gen_mtx = data.read(1)
        self.padding = data.read(1)

    @classmethod
    def from_json(cls, data: bytes):
        self = cls.__new__(cls)

        self.tex_gen_type = data[0].to_bytes(1, 'big')
        self.tex_gen_src = data[1].to_bytes(1, 'big')
        self.tex_gen_mtx = data[2].to_bytes(1, 'big')
        self.padding = data[3].to_bytes(1, 'big')

        return self
# --------------------------------------


# Tex Mtx Info -------------------------
class Tex_Mtx_Info_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        self.tex_mtx_info: List[J2D_Tex_Mtx_Info] = []
        self.padding = bytes()
        if offsets.tex_mtx_info_offset > 0:
            for i in range(initData.tex_mtx_info_count):
                self.tex_mtx_info.insert(i, J2D_Tex_Mtx_Info(data))
            next_offset = offsets.find_next_valid_offset(offsets.tex_mtx_info_offset)
            self.padding = data.read((next_offset - offsets.tex_mtx_info_offset) - ((initData.tex_mtx_info_count) * 36))
        self.size = len(self.tex_mtx_info) * 36

    def to_bytes(self):
        out = bytearray()
        for texMtxInfo in self.tex_mtx_info:
            out += texMtxInfo.to_bytes()
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        unique_bytes = []
        for material in obj["Materials"]:
            for entry in material["tex_matrices"]:
                if entry is None:
                    continue

                data = bytes.fromhex(str(entry))

                if data not in unique_bytes:
                    unique_bytes.append(data)

        self.tex_mtx_info = []
        for i in range(len(unique_bytes)):
            self.tex_mtx_info.insert(i, J2D_Tex_Mtx_Info.from_json(unique_bytes[i]))

        self.size = len(self.tex_mtx_info) * 36
        self.padding = b''

        return self


class J2D_Tex_Mtx_Info:
    def __init__(self, data: BytesIO):
        self.tex_mtx_type = data.read(1)
        self.tex_mtx_dcc = data.read(1)
        self.field_0x2 = data.read(1)
        self.field_0x3 = data.read(1)
        self.center_x = struct.unpack('>f', data.read(4))[0]
        self.center_y = struct.unpack('>f', data.read(4))[0]
        self.center_z = struct.unpack('>f', data.read(4))[0]
        self.tex_srt_info = J2D_Texture_SRT_Info(data)

    def to_bytes(self):
        out = bytearray()
        out += self.tex_mtx_type
        out += self.tex_mtx_dcc
        out += self.field_0x2
        out += self.field_0x3
        out += struct.pack('>f', self.center_x)
        out += struct.pack('>f', self.center_y)
        out += struct.pack('>f', self.center_z)
        out += self.tex_srt_info.to_bytes()
        return out

    @classmethod
    def from_json(cls, byte_chunk: list):
        self = cls.__new__(cls)

        data = [byte_chunk[i:i+4] for i in range(0, len(byte_chunk), 4)]
        self.tex_mtx_type = data[0][0].to_bytes(1, 'big')
        self.tex_mtx_dcc = data[0][1].to_bytes(1, 'big')
        self.field_0x2 = data[0][2].to_bytes(1, 'big')
        self.field_0x3 = data[0][3].to_bytes(1, 'big')
        self.center_x = struct.unpack('>f', data[1])[0]
        self.center_y = struct.unpack('>f', data[2])[0]
        self.center_z = struct.unpack('>f', data[3])[0]
        self.tex_srt_info = J2D_Texture_SRT_Info.from_json(data)

        return self


class J2D_Texture_SRT_Info:
    def __init__(self, data: BytesIO):
        self.scale_x = struct.unpack('>f', data.read(4))[0]
        self.scale_y = struct.unpack('>f', data.read(4))[0]
        self.rotation_deg = struct.unpack('>f', data.read(4))[0]
        self.translation_x = struct.unpack('>f', data.read(4))[0]
        self.translation_y = struct.unpack('>f', data.read(4))[0]

    def to_bytes(self):
        out = bytearray()
        out += struct.pack('>f', self.scale_x)
        out += struct.pack('>f', self.scale_y)
        out += struct.pack('>f', self.rotation_deg)
        out += struct.pack('>f', self.translation_x)
        out += struct.pack('>f', self.translation_y)
        return out

    @classmethod
    def from_json(cls, data: list[list]):
        self = cls.__new__(cls)

        self.scale_x = struct.unpack('>f', data[4])[0]
        self.scale_y = struct.unpack('>f', data[5])[0]
        self.rotation_deg = struct.unpack('>f', data[6])[0]
        self.translation_x = struct.unpack('>f', data[7])[0]
        self.translation_y = struct.unpack('>f', data[7])[0]

        return self
# --------------------------------------


# Tex No -------------------------------
class Tex_No_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        self.tex_no: List[bytes] = []
        self.padding = bytes()
        if offsets.tex_no_offset > 0:
            for i in range(initData.tex_no_count):
                self.tex_no.insert(i, data.read(2))
            next_offset = offsets.find_next_valid_offset(offsets.tex_no_offset)
            self.padding = data.read((next_offset - offsets.tex_no_offset) - ((initData.tex_no_count) * 2))
        # Calculate size
        self.size = len(self.tex_no) * 2
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for texNo in self.tex_no:
            out += texNo
        out += self.padding
        return out

    @classmethod
    def from_json(cls, num_of_textures: int):
        self = cls.__new__(cls)

        self.tex_no = []
        for i in range(num_of_textures):
            self.tex_no.insert(i, i.to_bytes(2, 'big'))

        # Calculate Padding
        self.size = num_of_textures * 2
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self
# --------------------------------------


# Font No ------------------------------
class Font_No_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        self.font_no: List[bytes] = []
        self.padding = bytes()
        if offsets.font_no_offset > 0:
            for i in range(initData.font_no_count):
                self.font_no.insert(i, data.read(2))
            next_offset = offsets.find_next_valid_offset(offsets.font_no_offset)
            self.padding = data.read((next_offset - offsets.font_no_offset) - ((initData.font_no_count) * 2))
        # Calculate size
        self.size = len(self.font_no) * 2
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for fontNo in self.font_no:
            out += fontNo
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        unique_font_entries = []
        for material in obj["Materials"]:
            if material["font"] is None:
                continue

            data = bytes.fromhex(material["font"])

            if data not in unique_font_entries:
                unique_font_entries.append(data)

        self.font_no = []
        for i in range(len(unique_font_entries)):
            self.font_no.insert(i, unique_font_entries[i])

        # Calculate Padding
        self.size = len(self.font_no) * 2
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self
# --------------------------------------


# Tev Order Info -----------------------
class Tev_Order_Info_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.tev_order_info_offset > 0:
            self.tev_order_info: List[J2D_Tev_Order_Info] = []
            for i in range(initData.tev_order_info_count):
                self.tev_order_info.insert(i, J2D_Tev_Order_Info(data))
            next_offset = offsets.find_next_valid_offset(offsets.tev_order_info_offset)
            self.padding = data.read((next_offset - offsets.tev_order_info_offset) - ((initData.tev_order_info_count) * 4))
        # Calculate size
        self.size = len(self.tev_order_info) * 4
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for tevOrderInfo in self.tev_order_info:
            out += tevOrderInfo.tex_coord
            out += tevOrderInfo.tex_map
            out += tevOrderInfo.color
            out += tevOrderInfo.field_0x3
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Gather every *unique* J2DTevOrderInfo data instance
        self.tev_order_info = []
        idx = int(0)
        seen = set()
        for material in obj["Materials"]:
            for tevOrderInfo in material["tevorders"]:
                if tevOrderInfo is None:
                    continue

                tevOrderInfoObj = J2D_Tev_Order_Info.from_json(bytes.fromhex(tevOrderInfo))
                key = (tevOrderInfoObj.tex_coord, tevOrderInfoObj.tex_map, tevOrderInfoObj.color, tevOrderInfoObj.field_0x3)
                if key not in seen:
                    self.tev_order_info.insert(idx, tevOrderInfoObj)
                    seen.add(key)
                    idx += 1

        # Calculate Padding
        self.size = len(self.tev_order_info) * 4
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self


class J2D_Tev_Order_Info:
    def __init__(self, data: BytesIO):
        self.tex_coord = data.read(1)
        self.tex_map = data.read(1)
        self.color = data.read(1)
        self.field_0x3 = data.read(1)

    @classmethod
    def from_json(cls, data: bytes):
        self = cls.__new__(cls)

        self.tex_coord = data[0].to_bytes(1, 'big')
        self.tex_map = data[1].to_bytes(1, 'big')
        self.color = data[2].to_bytes(1, 'big')
        self.field_0x3 = data[3].to_bytes(1, 'big')

        return self
# --------------------------------------


# Tev Color ----------------------------
class Tev_Color_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.tev_color_offset > 0:
            self.tev_color: List[GXColorS10] = []
            for i in range(initData.tev_color_count):
                self.tev_color.insert(i, GXColorS10(data))
            next_offset = offsets.find_next_valid_offset(offsets.tev_color_offset)
            self.padding = data.read((next_offset - offsets.tev_color_offset) - ((initData.tev_color_count) * 8))
        # Calculate size
        self.size = len(self.tev_color) * 8
        i = 0
        while self.size % 8 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for tevColor in self.tev_color:
            out += tevColor.r
            out += tevColor.g
            out += tevColor.b
            out += tevColor.a
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Gather every *unique* Tev Color data instance
        self.tev_color = []
        idx = int(0)
        seen = set()
        for material in obj["Materials"]:
            for tevColor in material["tevcolors"]:
                if tevColor is None:
                    continue

                tevColorObj = GXColorS10.from_json(bytes.fromhex(tevColor))
                key = (tevColorObj.r, tevColorObj.g, tevColorObj.b, tevColorObj.a)
                if key not in seen:
                    self.tev_color.insert(idx, tevColorObj)
                    seen.add(key)
                    idx += 1

        # Calculate Padding
        self.size = len(self.tev_color) * 8
        i = 0
        self.padding = b''
        while self.size % 8 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self


class GXColorS10:
    def __init__(self, data: BytesIO):
        self.r = data.read(2)
        self.g = data.read(2)
        self.b = data.read(2)
        self.a = data.read(2)

    @classmethod
    def from_json(cls, data: bytes):
        self = cls.__new__(cls)

        self.r = data[0].to_bytes(1, 'big') + data[1].to_bytes(1, 'big')
        self.g = data[2].to_bytes(1, 'big') + data[3].to_bytes(1, 'big')
        self.b = data[4].to_bytes(1, 'big') + data[5].to_bytes(1, 'big')
        self.a = data[6].to_bytes(1, 'big') + data[7].to_bytes(1, 'big')

        return self
# --------------------------------------


# TevK Color ---------------------------
class Tev_K_Color_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.tev_k_color_offset > 0:
            self.tev_k_color: List[GXColor] = []
            for i in range(initData.tev_k_color_count):
                self.tev_k_color.insert(i, GXColor(data))
            next_offset = offsets.find_next_valid_offset(offsets.tev_k_color_offset)
            self.padding = data.read((next_offset - offsets.tev_k_color_offset) - ((initData.tev_k_color_count) * 4))
        # Calculate size
        self.size = len(self.tev_k_color) * 4
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for tevKColor in self.tev_k_color:
            out += tevKColor.r
            out += tevKColor.g
            out += tevKColor.b
            out += tevKColor.a
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Gather every *unique* TevK Color data instance
        self.tev_k_color = []
        idx = int(0)
        seen = set()
        for material in obj["Materials"]:
            for tevKColor in material["tevkcolors"]:
                if tevKColor is None:
                    continue

                tevKColorObj = GXColor.from_json(bytes.fromhex(tevKColor))
                key = (tevKColorObj.r, tevKColorObj.g, tevKColorObj.b, tevKColorObj.a)
                if key not in seen:
                    self.tev_k_color.insert(idx, tevKColorObj)
                    seen.add(key)
                    idx += 1

        # Calculate Padding
        self.size = len(self.tev_k_color) * 4
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self
# --------------------------------------


# Tev Stage Num ------------------------
class Tev_Stage_Num_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.tev_stage_num_offset > 0:
            self.tev_stage_num: List[bytes] = []
            for i in range(initData.tev_stage_num_count):
                self.tev_stage_num.insert(i, data.read(1))
            next_offset = offsets.find_next_valid_offset(offsets.tev_stage_num_offset)
            self.padding = data.read((next_offset - offsets.tev_stage_num_offset) - (initData.tev_stage_num_count))
        # Calculate size
        self.size = len(self.tev_stage_num)
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for tevStageNum in self.tev_stage_num:
            out += tevStageNum
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        self.tev_stage_num = []
        idx = int(0)
        seen = set()
        for material in obj["Materials"]:
            if material["tev_stage_count"] is None:
                continue

            key = (material["tev_stage_count"])
            if key not in seen:
                self.tev_stage_num.insert(idx, int(material["tev_stage_count"]).to_bytes(1, 'big'))
                seen.add(key)
                idx += 1

        # Calculate Padding
        self.size = len(self.tev_stage_num)
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self
# --------------------------------------


# Tev Stage Info -----------------------
class Tev_Stage_Info_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.tev_stage_info_offset > 0:
            self.tev_stage_info: List[J2D_Tev_Stage_Info] = []
            for i in range(initData.tev_stage_info_count):
                self.tev_stage_info.insert(i, J2D_Tev_Stage_Info(data))
            next_offset = offsets.find_next_valid_offset(offsets.tev_stage_info_offset)
            self.padding = data.read((next_offset - offsets.tev_stage_info_offset) - ((initData.tev_stage_info_count) * 20))
        # Calculate size
        self.size = len(self.tev_stage_info) * 20
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for tevStageInfo in self.tev_stage_info:
            out += tevStageInfo.to_bytes()
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        # Gather every *unique* Tev Stage Info data instance
        self.tev_stage_info = []
        idx = int(0)
        seen = set()
        for material in obj["Materials"]:
            for entry in material["tevstages"]:
                if entry is None:
                    continue

                tevStageInfoObj = J2D_Tev_Stage_Info.from_json(bytes.fromhex(entry))
                key = (tevStageInfoObj.field_0x0, tevStageInfoObj.color_a, tevStageInfoObj.color_b, tevStageInfoObj.color_c,
                       tevStageInfoObj.color_d, tevStageInfoObj.c_op, tevStageInfoObj.c_bias, tevStageInfoObj.c_scale,
                       tevStageInfoObj.c_clamp, tevStageInfoObj.c_reg, tevStageInfoObj.alpha_a, tevStageInfoObj.alpha_b,
                       tevStageInfoObj.alpha_c, tevStageInfoObj.alpha_d, tevStageInfoObj.a_op, tevStageInfoObj.a_bias,
                       tevStageInfoObj.a_scale, tevStageInfoObj.a_clamp, tevStageInfoObj.a_reg, tevStageInfoObj.field_0x13)
                if key not in seen:
                    self.tev_stage_info.insert(idx, tevStageInfoObj)
                    seen.add(key)
                    idx += 1

        # Calculate Padding
        self.size = len(self.tev_stage_info) * 20
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self


class J2D_Tev_Stage_Info:
    def __init__(self, data: BytesIO):
        self.field_0x0 = data.read(1)
        self.color_a = data.read(1)
        self.color_b = data.read(1)
        self.color_c = data.read(1)
        self.color_d = data.read(1)
        self.c_op = data.read(1)
        self.c_bias = data.read(1)
        self.c_scale = data.read(1)
        self.c_clamp = data.read(1)
        self.c_reg = data.read(1)
        self.alpha_a = data.read(1)
        self.alpha_b = data.read(1)
        self.alpha_c = data.read(1)
        self.alpha_d = data.read(1)
        self.a_op = data.read(1)
        self.a_bias = data.read(1)
        self.a_scale = data.read(1)
        self.a_clamp = data.read(1)
        self.a_reg = data.read(1)
        self.field_0x13 = data.read(1)

    def to_bytes(self):
        out = bytearray()
        out += self.field_0x0
        out += self.color_a
        out += self.color_b
        out += self.color_c
        out += self.color_d
        out += self.c_op
        out += self.c_bias
        out += self.c_scale
        out += self.c_clamp
        out += self.c_reg
        out += self.alpha_a
        out += self.alpha_b
        out += self.alpha_c
        out += self.alpha_d
        out += self.a_op
        out += self.a_bias
        out += self.a_scale
        out += self.a_clamp
        out += self.a_reg
        out += self.field_0x13
        return out

    @classmethod
    def from_json(cls, data: bytes):
        self = cls.__new__(cls)

        self.field_0x0 = data[0].to_bytes(1, 'big')
        self.color_a = data[1].to_bytes(1, 'big')
        self.color_b = data[2].to_bytes(1, 'big')
        self.color_c = data[3].to_bytes(1, 'big')
        self.color_d = data[4].to_bytes(1, 'big')
        self.c_op = data[5].to_bytes(1, 'big')
        self.c_bias = data[6].to_bytes(1, 'big')
        self.c_scale = data[7].to_bytes(1, 'big')
        self.c_clamp = data[8].to_bytes(1, 'big')
        self.c_reg = data[9].to_bytes(1, 'big')
        self.alpha_a = data[10].to_bytes(1, 'big')
        self.alpha_b = data[11].to_bytes(1, 'big')
        self.alpha_c = data[12].to_bytes(1, 'big')
        self.alpha_d = data[13].to_bytes(1, 'big')
        self.a_op = data[14].to_bytes(1, 'big')
        self.a_bias = data[15].to_bytes(1, 'big')
        self.a_scale = data[16].to_bytes(1, 'big')
        self.a_clamp = data[17].to_bytes(1, 'big')
        self.a_reg = data[18].to_bytes(1, 'big')
        self.field_0x13 = data[19].to_bytes(1, 'big')

        return self
# --------------------------------------


# Tev Swap Mode Info -------------------
class Tev_Swap_Mode_Info_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.tev_swap_mode_info_offset > 0:
            self.tev_swap_mode_info: List[J2D_Tev_Swap_Mode_Info] = []
            for i in range(initData.tev_swap_mode_info_count):
                self.tev_swap_mode_info.insert(i, J2D_Tev_Swap_Mode_Info(data))
            next_offset = offsets.find_next_valid_offset(offsets.tev_swap_mode_info_offset)
            self.padding = data.read((next_offset - offsets.tev_swap_mode_info_offset) - ((initData.tev_swap_mode_info_count) * 4))
        # Calculate size
        self.size = len(self.tev_swap_mode_info) * 4
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for tevSwapModeInfo in self.tev_swap_mode_info:
            out += tevSwapModeInfo.ras_sel
            out += tevSwapModeInfo.tex_sel
            out += tevSwapModeInfo.field_0x2
            out += tevSwapModeInfo.field_0x3
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        unique_bytes = []
        highest_idx = -1

        for material in obj["Materials"]:
            for entry in material["tevstage_swapmodes"]:
                if entry is None:
                    continue

                idx_str, data_str = entry.split(";")
                idx = int(idx_str.split("=")[1])
                data = bytes.fromhex(data_str)
                if idx > highest_idx:
                    highest_idx = idx

                if data not in unique_bytes:
                    unique_bytes.append(data)

        self.tev_swap_mode_info = []
        for i in range(highest_idx + 1):
            self.tev_swap_mode_info.insert(i, J2D_Tev_Swap_Mode_Info.from_json(unique_bytes[0]))

        # Calculate Padding
        self.size = len(self.tev_swap_mode_info) * 4
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self


class J2D_Tev_Swap_Mode_Info:
    def __init__(self, data: BytesIO):
        self.ras_sel = data.read(1)
        self.tex_sel = data.read(1)
        self.field_0x2 = data.read(1)
        self.field_0x3 = data.read(1)

    @classmethod
    def from_json(cls, data: bytes):
        self = cls.__new__(cls)

        self.ras_sel = data[0].to_bytes(1, 'big')
        self.tex_sel = data[1].to_bytes(1, 'big')
        self.field_0x2 = data[2].to_bytes(1, 'big')
        self.field_0x3 = data[3].to_bytes(1, 'big')

        return self
# --------------------------------------


# Tev Swap Mode Table Info -------------
class Tev_Swap_Mode_Table_Info_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.tev_swap_mode_table_info_offset > 0:
            self.tev_swap_mode_table_info: List[J2D_Tev_Swap_Mode_Table_Info] = []
            for i in range(initData.tev_swap_mode_table_info_count):
                self.tev_swap_mode_table_info.insert(i, J2D_Tev_Swap_Mode_Table_Info(data))
            next_offset = offsets.find_next_valid_offset(offsets.tev_swap_mode_table_info_offset)
            self.padding = data.read((next_offset - offsets.tev_swap_mode_table_info_offset) - ((initData.tev_swap_mode_table_info_count) * 4))
        # Calculate size
        self.size = len(self.tev_swap_mode_table_info) * 4
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for tevSwapModeTblInfo in self.tev_swap_mode_table_info:
            out += tevSwapModeTblInfo.field_0x0
            out += tevSwapModeTblInfo.field_0x1
            out += tevSwapModeTblInfo.field_0x2
            out += tevSwapModeTblInfo.field_0x3
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        unique_bytes = []
        count = 0

        for material in obj["Materials"]:
            for entry in material["tev_swapmode_tables"]:
                if entry is None:
                    continue

                data = bytes.fromhex(str(entry))

                if data not in unique_bytes:
                    unique_bytes.append(data)
                    count += 1

        self.tev_swap_mode_table_info = []
        for i in range(count):
            self.tev_swap_mode_table_info.insert(i, J2D_Tev_Swap_Mode_Table_Info.from_json(unique_bytes[i]))

        # Calculate Padding
        self.size = len(self.tev_swap_mode_table_info) * 4
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self


class J2D_Tev_Swap_Mode_Table_Info:
    def __init__(self, data: BytesIO):
        self.field_0x0 = data.read(1)
        self.field_0x1 = data.read(1)
        self.field_0x2 = data.read(1)
        self.field_0x3 = data.read(1)

    @classmethod
    def from_json(cls, data: bytes):
        self = cls.__new__(cls)

        self.field_0x0 = data[0].to_bytes(1, 'big')
        self.field_0x1 = data[1].to_bytes(1, 'big')
        self.field_0x2 = data[2].to_bytes(1, 'big')
        self.field_0x3 = data[3].to_bytes(1, 'big')

        return self
# --------------------------------------


# Alpha Comp Info ----------------------
class Alpha_Comp_Info_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.alpha_comp_info_offset > 0:
            self.alpha_comp_info: List[J2D_Alpha_Comp_Info] = []
            for i in range(initData.alpha_comp_info_count):
                self.alpha_comp_info.insert(i, J2D_Alpha_Comp_Info(data))
            next_offset = offsets.find_next_valid_offset(offsets.alpha_comp_info_offset)
            self.padding = data.read((next_offset - offsets.alpha_comp_info_offset) - ((initData.alpha_comp_info_count) * 8))
        # Calculate size
        self.size = len(self.alpha_comp_info) * 8
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for alphaCompInfo in self.alpha_comp_info:
            out += alphaCompInfo.field_0x0
            out += alphaCompInfo.field_0x1
            out += alphaCompInfo.ref_0
            out += alphaCompInfo.ref_1
            out += alphaCompInfo.field_0x4
            out += alphaCompInfo.field_0x5
            out += alphaCompInfo.field_0x6
            out += alphaCompInfo.field_0x7
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        unique_bytes = []
        count = 0

        for material in obj["Materials"]:
            if material["alphacomp"] is None:
                continue

            data = bytes.fromhex(str(material["alphacomp"]))

            if data not in unique_bytes:
                unique_bytes.append(data)
                count += 1

        self.alpha_comp_info = []
        for i in range(count):
            self.alpha_comp_info.insert(i, J2D_Alpha_Comp_Info.from_json(unique_bytes[i]))

        # Calculate Padding
        self.size = len(self.alpha_comp_info) * 8
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self


class J2D_Alpha_Comp_Info:
    def __init__(self, data: BytesIO):
        self.field_0x0 = data.read(1)
        self.field_0x1 = data.read(1)
        self.ref_0 = data.read(1)
        self.ref_1 = data.read(1)
        self.field_0x4 = data.read(1)
        self.field_0x5 = data.read(1)
        self.field_0x6 = data.read(1)
        self.field_0x7 = data.read(1)

    @classmethod
    def from_json(cls, data: bytes):
        self = cls.__new__(cls)

        self.field_0x0 = data[0].to_bytes(1, 'big')
        self.field_0x1 = data[1].to_bytes(1, 'big')
        self.ref_0 = data[2].to_bytes(1, 'big')
        self.ref_1 = data[3].to_bytes(1, 'big')
        self.field_0x4 = data[4].to_bytes(1, 'big')
        self.field_0x5 = data[5].to_bytes(1, 'big')
        self.field_0x6 = data[6].to_bytes(1, 'big')
        self.field_0x7 = data[7].to_bytes(1, 'big')

        return self
# --------------------------------------


# Blend Info ---------------------------
class Blend_Info_Section:
    def __init__(self, data: BytesIO, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.blend_info_offset > 0:
            self.blend_info: List[J2D_Blend_Info] = []
            for i in range(initData.blend_info_count):
                self.blend_info.insert(i, J2D_Blend_Info(data))
            next_offset = offsets.find_next_valid_offset(offsets.blend_info_offset)
            self.padding = data.read((next_offset - offsets.blend_info_offset) - ((initData.blend_info_count) * 4))
        # Calculate size
        self.size = len(self.blend_info) * 4
        i = 0
        while self.size % 4 != 0:
            self.size += 1
            i += 1

    def to_bytes(self):
        out = bytearray()
        for blendInfo in self.blend_info:
            out += blendInfo.type
            out += blendInfo.src_factor
            out += blendInfo.dst_factor
            out += blendInfo.op
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        unique_bytes = []
        count = 0

        for material in obj["Materials"]:
            if material["blend"] is None:
                continue

            data = bytes.fromhex(str(material["blend"]))

            if data not in unique_bytes:
                unique_bytes.append(data)
                count += 1

        self.blend_info = []
        for i in range(count):
            self.blend_info.insert(i, J2D_Blend_Info.from_json(unique_bytes[i]))

        # Calculate Padding
        self.size = len(self.blend_info) * 4
        i = 0
        self.padding = b''
        while self.size % 4 != 0:
            self.padding += padding_bytes[i]
            self.size += 1
            i += 1

        return self


class J2D_Blend_Info:
    def __init__(self, data: BytesIO):
        self.type = data.read(1)
        self.src_factor = data.read(1)
        self.dst_factor = data.read(1)
        self.op = data.read(1)

    @classmethod
    def from_json(cls, data: bytes):
        self = cls.__new__(cls)

        self.type = data[0].to_bytes(1, 'big')
        self.src_factor = data[1].to_bytes(1, 'big')
        self.dst_factor = data[2].to_bytes(1, 'big')
        self.op = data[3].to_bytes(1, 'big')

        return self
# --------------------------------------


# Dither -------------------------------
class Dither_Section:
    def __init__(self, data: BytesIO, mat1Section: MAT1_Section, offsets: MAT1_Section_Offsets, initData: Mat_Init_Data_Section):
        self.size = 0
        if offsets.dither_offset > 0:
            self.dither: List[bytes] = []
            for i in range(initData.dither_count):
                self.dither.insert(i, data.read(1))
            # This last padding formula is a little different as there is not a "next offset" to calculate with
            self.padding = data.read((mat1Section.size - offsets.dither_offset) - ((initData.dither_count)))
        self.size = len(self.dither)

    def to_bytes(self):
        out = bytearray()
        for dither in self.dither:
            out += dither
        out += self.padding
        return out

    @classmethod
    def from_json(cls, obj: dict):
        self = cls.__new__(cls)

        unique_bytes = []
        count = 0

        for material in obj["Materials"]:
            if material["dither"] is None:
                continue

            data = int(material["dither"]).to_bytes(1, 'big')

            if data not in unique_bytes:
                unique_bytes.append(data)
                count += 1

        self.dither = [b'\x00']
        for i in range(len(unique_bytes)):
            self.dither.insert(i + 1, unique_bytes[i])

        self.size = len(self.dither)
        self.padding = b''

        return self
# --------------------------------------
# --------------------------------------------------------------------------------------


global_block_counter = int(0)


# Elements -----------------------------------------------------------------------------
class PAN1:
    def __init__(self):
        self.magic = bytes()
        self.size = 0
        self.param_num = bytes()
        self.visible = bytes()
        self.padding = bytes()
        self.info_tag = bytes()
        self.bounds_x0 = bytes()
        self.bounds_y0 = bytes()
        self.panel_height = bytes()
        self.panel_width = bytes()
        self.angle = bytes()
        self.anchor = bytes()
        self.alpha = bytes()
        self.inherit_alpha = bytes()


class PAN2:
    def __init__(self, data: BytesIO):
        # Check for BGN1 tag
        self.start_tag = bytes()
        self.start_tag_size = int()
        old_pos = data.tell()
        tag = data.read(4)
        if tag == b'BGN1':
            self.start_tag = tag
            self.start_tag_size = bytes_to_int(data.read(4))
        else:
            data.seek(old_pos)

        # Parse PAN2 data
        self.magic = data.read(4)
        self.size = bytes_to_int(data.read(4))
        self.field_0x8 = data.read(2)
        self.bck_idx = data.read(2)
        self.visible = data.read(1)
        self.base_position = data.read(1)
        self.padding = data.read(2)
        self.info_tag = data.read(8)
        self.user_info_tag = data.read(8)
        self.size_x = struct.unpack('>f', data.read(4))[0]
        self.size_y = struct.unpack('>f', data.read(4))[0]
        self.scale_x = struct.unpack('>f', data.read(4))[0]
        self.scale_y = struct.unpack('>f', data.read(4))[0]
        self.rotate_x = struct.unpack('>f', data.read(4))[0]
        self.rotate_y = struct.unpack('>f', data.read(4))[0]
        self.rotate_z = struct.unpack('>f', data.read(4))[0]
        self.translate_x = struct.unpack('>f', data.read(4))[0]
        self.translate_y = struct.unpack('>f', data.read(4))[0]
        self.end_padding = data.read(4)
        self.end_tag = bytes()
        self.end_tag_size = int()

        # Empty for normal parsing - Used in json parsing
        self.bgn1_tag = b''
        self.bgn1_tag_size = int(0)

        self.child_nodes: List[Union[PAN2 | PIC2 | TBX2 | WIN2]] = []

        pos = data.tell()
        next_tag = data.read(4)
        data.seek(pos)

        if next_tag == b'BGN1':
            pos = data.tell()
            self.bgn1_tag = data.read(4)
            self.bgn1_tag_size = bytes_to_int(data.read(4))

            while True:
                child_pos = data.tell()
                tag = data.read(4)

                if tag == b'END1':
                    self.end_tag = tag
                    self.end_tag_size = bytes_to_int(data.read(4))
                    break

                data.seek(child_pos)

                match tag:
                    case b'PAN2':
                        self.child_nodes.append(PAN2(data))
                    case b'PIC2':
                        self.child_nodes.append(PIC2(data))
                    case b'TBX2':
                        self.child_nodes.append(TBX2(data))
                    case b'WIN2':
                        self.child_nodes.append(WIN2(data))
                    case _:
                        break

        # if isParent:
        #     while True:
        #         old_pos = data.tell()
        #         tag = data.read(4)
        #         self.tag = tag
        #         if tag == b'BGN1':
        #             tag_size = bytes_to_int(data.read(4))
        #             data.seek(old_pos)
        #             data.read(tag_size)
        #             # Determine child node(s) type
        #             magic = data.read(4)
        #             data.seek(old_pos)
        #             match magic:
        #                 case b'PAN2':
        #                     self.child_nodes.append(PAN2(data, True))
        #                 case b'PIC2':
        #                     self.child_nodes.append(PIC2(data))
        #                 case b'TBX2':
        #                     self.child_nodes.append(TBX2(data))
        #                 case b'WIN2':
        #                     self.child_nodes.append(WIN2(data))
        #         elif tag == b'PAN2':
        #             data.seek(old_pos)
        #             self.child_nodes.append(PAN2(data, True))
        #         elif tag == b'PIC2':
        #             data.seek(old_pos)
        #             self.child_nodes.append(PIC2(data))
        #         elif tag == b'TBX2':
        #             data.seek(old_pos)
        #             self.child_nodes.append(TBX2(data))
        #         elif tag == b'WIN2':
        #             data.seek(old_pos)
        #             self.child_nodes.append(WIN2(data))
        #         else:
        #             self.end_tag = tag
        #             self.end_tag_size = bytes_to_int(data.read(4))
        #             break

    def to_bytes(self):
        out = bytearray()
        if self.start_tag == b'BGN1':
            out += self.start_tag
            out += self.start_tag_size.to_bytes(4, 'big')
        out += self.magic
        out += self.size.to_bytes(4, 'big')
        out += self.field_0x8
        out += self.bck_idx
        out += self.visible
        out += self.base_position
        out += self.padding
        out += self.info_tag
        out += self.user_info_tag
        out += struct.pack('>f', self.size_x)
        out += struct.pack('>f', self.size_y)
        out += struct.pack('>f', self.scale_x)
        out += struct.pack('>f', self.scale_y)
        out += struct.pack('>f', self.rotate_x)
        out += struct.pack('>f', self.rotate_y)
        out += struct.pack('>f', self.rotate_z)
        out += struct.pack('>f', self.translate_x)
        out += struct.pack('>f', self.translate_y)
        out += self.end_padding

        if self.bgn1_tag_size > 0:
            out += self.bgn1_tag
            out += self.bgn1_tag_size.to_bytes(4, 'big')

        for node in self.child_nodes:
            magic = node.magic
            match magic:
                case b'PAN2':
                    out += node.to_bytes()

                case b'PIC2':
                    out += node.to_bytes()

                case b'TBX2':
                    out += node.to_bytes()

                case b'WIN2':
                    out += node.to_bytes()

        out += self.end_tag
        if self.end_tag_size > 0:
            out += self.end_tag_size.to_bytes(4, 'big')
        return out

    @classmethod
    def from_json(cls, root_node: dict, child_list: list, is_root: bool, is_base_pan: bool):
        global global_block_counter
        global elements_size

        global_block_counter += 1

        self = cls.__new__(cls)

        self.start_tag = b''

        if not is_base_pan:
            self.magic = str(root_node["type"]).encode()
        else:
            self.magic = b'pan2'
        self.size = int(72)
        self.field_0x8 = int(64).to_bytes(2, 'big')
        self.bck_idx = int(root_node["p_bckindex"]).to_bytes(2, 'big')
        self.visible = int(root_node["p_enabled"]).to_bytes(1, 'big')
        self.base_position = int(root_node["p_anchor"]).to_bytes(1, 'big')
        self.padding = b'\x52'b'\x45'
        self.info_tag = str(root_node["p_panename"]).encode()
        self.user_info_tag = str(root_node["p_secondaryname"]).encode()
        self.size_x = root_node["p_size_x"]
        self.size_y = root_node["p_size_y"]
        self.scale_x = root_node["p_scale_x"]
        self.scale_y = root_node["p_scale_y"]
        self.rotate_x = root_node["p_rotation"]
        self.rotate_y = root_node["p_rotation"]
        self.rotate_z = root_node["p_rotation"]
        self.translate_x = root_node["p_offset_x"]
        self.translate_y = root_node["p_offset_y"]
        self.end_padding = struct.pack('>f', root_node["p_unk4"])
        self.bgn1_tag = b''
        self.bgn1_tag_size = 0

        self.child_nodes = []
        self.end_tag = b''
        self.end_tag_size = 0
        if not is_base_pan:
            i = 0
            while i < len(child_list):
                item = child_list[i]

                if isinstance(item, dict):
                    node = item
                    children = []

                    # Check if next item is a list
                    if i + 1 < len(child_list) and isinstance(child_list[i + 1], list):
                        children = child_list[i + 1]
                        i += 1

                    if node["type"] == "PAN2":
                        self.child_nodes.append(
                            PAN2.from_json(node, children, False, False)
                        )
                    elif node["type"] == "PIC2":
                        self.child_nodes.append(
                            PIC2.from_json(node, False)
                        )
                    elif node["type"] == "TBX2":
                        self.child_nodes.append(
                            TBX2.from_json(node)
                        )

                i += 1

            if not is_base_pan and len(self.child_nodes) > 0:
                self.bgn1_tag = b'BGN1'
                self.bgn1_tag_size = 8
                global_block_counter += 1

            if is_root:
                self.end_tag = b'END1'b'\x00'b'\x00'b'\x00'b'\x08'b'EXT1'
                self.end_tag_size = 8
                global_block_counter += 2
            elif len(self.child_nodes) > 0:
                self.end_tag = b'END1'
                self.end_tag_size = 8
                global_block_counter += 1
            else:
                self.end_tag = b''
                self.end_tag_size = 0

            elements_size += self.size + self.bgn1_tag_size + self.end_tag_size
        return self

    @classmethod
    def new_pan2(cls, data, is_base_pan: bool):
        self = cls.__new__(cls)

        self.start_tag = b''
        if not is_base_pan:
            self.magic = data["type"].encode()
        else:
            self.magic = b'pan2'
        self.size = int(72)
        self.field_0x8 = int(64).to_bytes(2, 'big')
        self.bck_idx = int(data["bck_idx"]).to_bytes(2, 'big')
        self.visible = int(data["enabled"]).to_bytes(1, 'big')
        self.base_position = get_anchor(data["anchor"]).to_bytes(1, 'big')
        self.padding = b'\x52'b'\x45'
        self.info_tag = str(data["element_name"]).encode()
        self.user_info_tag = str(data["secondary_name"]).encode()
        self.size_x = data["size_x"]
        self.size_y = data["size_y"]
        self.scale_x = data["scale_x"]
        self.scale_y = data["scale_y"]
        self.rotate_x = data["rotation"]
        self.rotate_y = data["rotation"]
        self.rotate_z = data["rotation"]
        self.translate_x = data["offset_x"]
        self.translate_y = data["offset_y"]
        self.end_padding = b'\x00' * 4
        self.child_nodes = []

        if data["has_children"] == "Yes":
            self.bgn1_tag = b'BGN1'
            self.bgn1_tag_size = 8
            self.end_tag = b'END1'
            self.end_tag_size = 8
        else:
            self.bgn1_tag = b''
            self.bgn1_tag_size = 0
            self.end_tag = b''
            self.end_tag_size = 0

        return self


class PIC2:
    def __init__(self, data: BytesIO):
        self.tag = bytes()
        self.tag_size = int()
        old_pos = data.tell()
        tag = data.read(4)
        if tag == b'BGN1':
            self.tag = tag
            self.tag_size = bytes_to_int(data.read(4))
        else:
            data.seek(old_pos)

        self.magic = data.read(4)
        self.size = bytes_to_int(data.read(4))
        self.base_pan2 = PAN2(data)
        self.field_0x0 = data.read(2)
        self.field_0x2 = data.read(2)
        self.material_num = data.read(2)
        self.field_0x6 = data.read(2)
        self.field_0x8: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2)]
        self.field_0x10: List[bytes] = [data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2), data.read(2)]
        self.corner_color: List[bytes] = [data.read(4), data.read(4), data.read(4), data.read(4)]

    def to_bytes(self):
        out = bytearray()
        if self.tag == b'BGN1':
            out += self.tag
            out += self.tag_size.to_bytes(4, 'big')
        out += self.magic
        out += self.size.to_bytes(4, 'big')
        out += self.base_pan2.to_bytes()
        out += self.field_0x0
        out += self.field_0x2
        out += self.material_num
        out += self.field_0x6
        for item in self.field_0x8:
            out += item
        for item in self.field_0x10:
            out += item
        for cornerColor in self.corner_color:
            out += cornerColor
        return out

    @classmethod
    def from_json(cls, node: dict, insert_end_tag: bool):
        global elements_size
        global global_element_counter

        self = cls.__new__(cls)

        self.tag = b''
        self.tag_size = 0
        self.magic = b'PIC2'
        self.size = int(128)
        elements_size += self.size
        self.base_pan2 = PAN2.from_json(node, [], False, True)
        self.field_0x0 = int(node["size"]).to_bytes(2, 'big')
        self.field_0x2 = int(node["unk_index"]).to_bytes(2, 'big')
        self.material_num = global_element_counter.to_bytes(2, 'big')
        global_element_counter += 1
        self.field_0x6 = b'\x52'b'\x45'
        self.field_0x8 = []
        self.field_0x8.append(int(node["color1"]["unk1"]).to_bytes(2, 'big'))
        self.field_0x8.append(int(node["color1"]["unk2"]).to_bytes(2, 'big'))
        self.field_0x8.append(int(node["color2"]["unk1"]).to_bytes(2, 'big'))
        self.field_0x8.append(int(node["color2"]["unk2"]).to_bytes(2, 'big'))
        self.field_0x10 = []
        for field in node["color1"]["unknowns"]:
            self.field_0x10.append(int(field).to_bytes(2, 'big'))
        for field in node["color2"]["unknowns"]:
            self.field_0x10.append(int(field).to_bytes(2, 'big'))
        self.corner_color = []
        color_bytes = [int(c).to_bytes(1, 'big') for c in node["color1"]["col1"]]
        self.corner_color.append(b''.join(color_bytes))
        color_bytes = [int(c).to_bytes(1, 'big') for c in node["color1"]["col2"]]
        self.corner_color.append(b''.join(color_bytes))
        color_bytes = [int(c).to_bytes(1, 'big') for c in node["color2"]["col1"]]
        self.corner_color.append(b''.join(color_bytes))
        color_bytes = [int(c).to_bytes(1, 'big') for c in node["color2"]["col2"]]
        self.corner_color.append(b''.join(color_bytes))

        return self

    @classmethod
    def new_pic2(cls, data):
        self = cls.__new__(cls)

        self.tag = b''
        self.magic = b'PIC2'
        self.size = int(128)
        self.base_pan2 = PAN2.new_pan2(data, True)
        self.field_0x0 = int(48).to_bytes(2, 'big')
        self.field_0x2 = data["unk_idx"].to_bytes(2, 'big')
        # self.material_num = b'\x00'b'\x00'
        self.material_num = data["mat_idx"].to_bytes(2, 'big')
        self.field_0x6 = b'\x52'b'\x45'
        self.field_0x8 = []  # handle in alphabetical walkthrough
        for field in data["unk_indexes"]:
            self.field_0x8.append(int(field).to_bytes(2, 'big'))
        self.field_0x10 = []
        for field in data["uv_coords"]:
            self.field_0x10.append(int(field).to_bytes(2, 'big'))
        self.corner_color = []
        for color in data["colors"]:
            self.corner_color.append(int(color).to_bytes(1, 'big'))

        return self


class TBX2:
    def __init__(self, data: BytesIO):
        self.tag = bytes()
        self.tag_size = int()
        old_pos = data.tell()
        tag = data.read(4)
        if tag == b'BGN1':
            self.tag = tag
            self.tag_size = bytes_to_int(data.read(4))
        else:
            data.seek(old_pos)

        current_pos = 0
        self.magic = data.read(4)
        self.size = bytes_to_int(data.read(4))
        self.base_pan2 = PAN2(data)
        self.field_0x0 = data.read(2)
        self.field_0x2 = data.read(2)
        self.material_num = data.read(2)
        self.char_space = data.read(2)
        self.line_space = data.read(2)
        self.font_size_x = data.read(2)
        self.font_size_y = data.read(2)
        self.h_bind = data.read(1)
        self.v_bind = data.read(1)
        self.char_color = data.read(4)
        self.grad_color = data.read(4)
        self.connected = data.read(1)
        self.field_0x19 = data.read(3)
        self.field_0x1c = data.read(2)
        self.field_0x1e = data.read(2)
        current_pos += 40 + self.base_pan2.size
        self.end_padding = data.read(self.size - current_pos)

    def to_bytes(self):
        out = bytearray()
        if self.tag == b'BGN1':
            out += self.tag
            out += self.tag_size.to_bytes(4, 'big')
        out += self.magic
        out += self.size.to_bytes(4, 'big')
        out += self.base_pan2.to_bytes()
        out += self.field_0x0
        out += self.field_0x2
        out += self.material_num
        out += self.char_space
        out += self.line_space
        out += self.font_size_x
        out += self.font_size_y
        out += self.h_bind
        out += self.v_bind
        out += self.char_color
        out += self.grad_color
        out += self.connected
        out += self.field_0x19
        out += self.field_0x1c
        out += self.field_0x1e
        out += self.end_padding
        return out

    @classmethod
    def from_json(cls, node: dict):
        global elements_size
        global global_element_counter

        self = cls.__new__(cls)

        self.tag = b''
        self.tag_size = 0
        self.magic = b'TBX2'
        self.base_pan2 = PAN2.from_json(node, [], False, True)
        self.field_0x0 = int(node["size"]).to_bytes(2, 'big')
        self.field_0x2 = int(node["unk1"]).to_bytes(2, 'big')
        self.material_num = global_element_counter.to_bytes(2, 'big')
        global_element_counter += 1
        self.char_space = int(node["signedunk3"]).to_bytes(2, 'big')
        self.line_space = int(node["signedunk4"]).to_bytes(2, 'big')
        self.font_size_x = int(node["unk5"]).to_bytes(2, 'big')
        self.font_size_y = int(node["unk6"]).to_bytes(2, 'big')
        self.h_bind = int(node["unk7byte"]).to_bytes(1, 'big')
        self.v_bind = int(node["unk8byte"]).to_bytes(1, 'big')
        self.char_color = b''.join([int(c).to_bytes(1, 'big') for c in node["color_top"]])
        self.grad_color = b''.join([int(c).to_bytes(1, 'big') for c in node["color_bottom"]])
        self.connected = int(node["unk11"]).to_bytes(1, 'big')
        self.field_0x19 = b'\x52'b'\x45'b'\x53'
        self.field_0x1c = int(node["text_cutoff"]).to_bytes(2, 'big')
        self.field_0x1e = len(str(node["text"]).encode('shift_jis')).to_bytes(2, 'big')
        self.end_padding = str(node["text"]).encode('shift_jis')

        i = 0
        while len(self.end_padding) % 8 != 0:
            self.end_padding += padding_bytes[i]
            i += 1

        self.size = int(112) + len(self.end_padding)
        elements_size += self.size

        return self
    
    @classmethod
    def new_tbx2(cls, data):
        self = cls.__new__(cls)

        self.tag = b''
        self.magic = b'TBX2'
        self.size = int(0)  # calculate size at end
        self.base_pan2 = PAN2.new_pan2(data, True)
        self.field_0x0 = int(48).to_bytes(2, 'big')
        self.field_0x2 = data["unk_idx"].to_bytes(2, 'big')
        self.material_num = b'\x00'b'\x00'
        # self.material_num = data["mat_idx"].to_bytes(2, 'big')
        self.char_space = int(data["char_space"]).to_bytes(2, 'big')
        self.line_space = int(data["line_space"]).to_bytes(2, 'big')
        self.font_size_x = int(data["font_size_x"]).to_bytes(2, 'big')
        self.font_size_y = int(data["font_size_y"]).to_bytes(2, 'big')
        self.h_bind = get_horiz_bind(data["h_bind"]).to_bytes(1, 'big')
        self.v_bind = get_vert_bind(data["v_bind"]).to_bytes(1, 'big')
        self.char_color = b''.join([int(c).to_bytes(1, 'big') for c in data["char_color"]])
        self.grad_color = b''.join([int(c).to_bytes(1, 'big') for c in data["grad_color"]])
        self.connected = int(data["connected"]).to_bytes(1, 'big')
        self.field_0x19 = b'\x52'b'\x45'b'\x53'
        self.field_0x1c = int(data["text_cutoff"]).to_bytes(2, 'big')
        self.field_0x1e = len(str(data["text"]).encode('shift_jis')).to_bytes(2, 'big')
        self.end_padding = str(data["text"]).encode('shift_jis')

        i = 0
        while len(self.end_padding) % 8 != 0:
            self.end_padding += padding_bytes[i]
            i += 1

        self.size = int(112) + len(self.end_padding)

        return self


def depth_first_search(node: PAN2 | PIC2 | TBX2, data):
    if (node.magic.decode() == "PIC2" or node.magic.decode() == "TBX2"):
        if data["parent"] in node.base_pan2.info_tag.decode():
            return node
    else:
        if data["parent"] in node.info_tag.decode():
            return node

    for child in node.child_nodes:
        if (child.magic.decode() == "PIC2" or child.magic.decode() == "TBX2"):
            if data["parent"] in child.base_pan2.info_tag.decode():
                return node
        else:
            result = depth_first_search(child, data)
            if result is not None:
                return result

    return None


def get_anchor(type: str) -> int:
    match type:
        case "Top-Left":
            return 0

        case "Center-Top":
            return 1

        case "Top-Right":
            return 2

        case "Center-Left":
            return 3

        case "Center":
            return 4

        case "Center-Right":
            return 5

        case "Bottom-Left":
            return 6

        case "Center-Bottom":
            return 7

        case "Bottom-Right":
            return 8


def get_horiz_bind(type: str) -> int:
    match type:
        case "Center":
            return 0
        
        case "Right":
            return 1
        
        case "Left":
            return 2


def get_vert_bind(type: str) -> int:
    match type:
        case "Center":
            return 0
        
        case "Bottom":
            return 1
        
        case "Top":
            return 2


def iterate_element_counter(root: PAN2):
    global global_element_counter

    for child in root.child_nodes:
        if child.magic.decode() == "PAN2":
            iterate_element_counter(child)
        elif child.magic.decode() == "PIC2":
            child.material_num = global_element_counter.to_bytes(2, 'big')
            global_element_counter += 1
        elif child.magic.decode() == "TBX2":
            child.material_num = global_element_counter.to_bytes(2, 'big')
            global_element_counter += 1


class WIN2:
    def __init__(self, data: BytesIO):
        self.tag = bytes()
        self.tag_size = int()
        old_pos = data.tell()
        tag = data.read(4)
        if tag == b'BGN1':
            self.tag = tag
            self.tag_size = bytes_to_int(data.read(4))
        else:
            data.seek(old_pos)

        self.magic = data.read(4)
        self.size = bytes_to_int(data.read(4))
        self.base_pan2 = PAN2(data)
        self.data = data.read((self.size - 8) - self.base_pan2.size)

    def to_bytes(self):
        out = bytearray()
        if self.tag == b'BGN1':
            out += self.tag
            out += self.tag_size.to_bytes(4, 'big')
        out += self.magic
        out += self.size.to_bytes(4, 'big')
        out += self.base_pan2.to_bytes()
        out += self.data
        return out
# --------------------------------------------------------------------------------------


def get_mat_init_data_entry(material_name: str):
    for blo_name, mat_map in blo_material_init_idxs.items():
        if material_name in mat_map:
            idx = mat_map[material_name]
            for blo in blos:
                if blo_name == blo.name:
                    return blo.mat1_section.mat_init_section.mat_init_data[idx]

def run_blo_editor(layout_folder: str | Path, patch_folder: str | Path, output_folder: str | Path):
    global blos, global_material_ids, blo_material_init_idxs, global_element_counter, elements_size

    blos.clear()
    global_material_ids.clear()
    blo_material_init_idxs.clear()
    global_element_counter = 0
    elements_size = 0

    layout_folder  = Path(layout_folder)
    patch_folder   = Path(patch_folder)
    output_folder  = Path(output_folder)

    if not layout_folder.exists():
        raise FileNotFoundError(f"Layout folder not found: {layout_folder}")
    if not patch_folder.exists():
        raise FileNotFoundError(f"Patch folder not found: {patch_folder}")

    output_folder.mkdir(parents=True, exist_ok=True)

    blo_arcs = list(layout_folder.glob("*.arc"))
    blo_arcs_data: List[BytesIO] = []
    blo_files: List[RARCFileEntry] = []

    for file_path in blo_arcs:
        with open(file_path, "rb") as f:
            blo_arcs_data.append(BytesIO(f.read()))

    for arc_data in blo_arcs_data:
        try:
            arc = RARC(arc_data)
            arc.read()
            for entry in arc.file_entries:
                if entry.name.lower().endswith(".blo"):
                    if not entry.name.lower().endswith("file_error.blo"):
                        entry.decompress_data_if_necessary()
                        blo_files.append(entry)
        except Exception as e:
            print(f"  [failed to read ARC: {e}]")
            continue

    print(f"Found {len(blo_files)} BLO files")
    for i, entry in enumerate(blo_files):
        blos.insert(i, parse_blo(entry.data))
        blos[i].name = entry.name

    def build_material_id_lookup(blos: List[BLO]):
        next_global_id = 0
        for blo in blos:
            mat_table = blo.mat1_section.mat_name_table_section
            blo_material_init_idxs[blo.name] = {}
            local_idx = 0
            for name, entry in zip(mat_table.mat_names, mat_table.header_entries):
                clean_name = name.rstrip(b'\x00').decode('ascii')
                if clean_name not in global_material_ids:
                    global_material_ids[clean_name] = entry.id
                    next_global_id += 1
                blo_material_init_idxs[blo.name][clean_name] = local_idx
                local_idx += 1

    build_material_id_lookup(blos)

    for i, blo in enumerate(blos):
        print(blo.name)
        blo.rebuild_blo(output_folder / blo.name)

    json_files = list(patch_folder.glob("*.json"))

    changed_arcs: dict[str, RARC] = {}
    for blo in blos:
        for json_filepath in json_files:
            with open(json_filepath, "r", encoding="utf-8") as f:
                json_data = json.load(f)
                if json_data[0]["blo_file"] != blo.name:
                    continue

                blo.edit_from_json(json_data)
                blo.size = blo.get_total_size()
                i = 0
                while blo.size % 16 != 0:
                    blo.size += 1
                    blo.padding += padding_bytes[i]
                    i += 1
                blo.rebuild_blo(output_folder / json_data[0]["new_filename"])

                arc_name = json_data[0]["arc"]
                if arc_name in changed_arcs:
                    continue

                for arc_path in blo_arcs:
                    if arc_name in arc_path.name and arc_path.suffix.lower().endswith(".arc"):
                        with open(arc_path, "rb") as f:
                            arc_data = BytesIO(f.read())
                        arc = RARC(arc_data)
                        arc.read()
                        changed_arcs[arc_name] = arc
                        break

    for json_filepath in json_files:
        with open(json_filepath, "r", encoding="utf-8") as f:
            json_data = json.load(f)
            match json_data[0]["action"]:
                case "add":
                    for entry in changed_arcs[json_data[0]["arc"]].file_entries:
                        if entry.name.lower().endswith(json_data[0]["blo_file"]):
                            new_file_data = BytesIO()
                            out_path = output_folder / json_data[0]["new_filename"]
                            if out_path.exists():
                                with open(out_path, "rb") as f:
                                    new_file_data = BytesIO(f.read())
                            target_node = RARCNode(changed_arcs[json_data[0]["arc"]])
                            for node in changed_arcs[json_data[0]["arc"]].nodes:
                                if node.name == "scrn":
                                    target_node = node
                                    break
                            changed_arcs[json_data[0]["arc"]].add_new_file(
                                json_data[0]["new_filename"], new_file_data, target_node
                            )
                            changed_arcs[json_data[0]["arc"]].save_changes()
            print(f"Built blo: {json_data[0]['new_filename']}")

    for json_filepath in json_files:
        with open(json_filepath, "r", encoding="utf-8") as f:
            json_data = json.load(f)
            for entry in blo_arcs:
                if entry.name.lower().startswith(json_data[0]["arc"]) and entry.name.lower().endswith(".arc"):
                    data = changed_arcs[json_data[0]["arc"]].data
                    out_path = layout_folder / json_data[0]["arc"]
                    print(out_path)
                    out_path.write_bytes(data.getvalue())

    print("Done")


if __name__ == "__main__":
    import sys
    if len(sys.argv) != 4:
        print("Usage: blo_editor.py <layout_folder> <patch_folder> <output_folder>")
        sys.exit(1)
    run_blo_editor(sys.argv[1], sys.argv[2], sys.argv[3])