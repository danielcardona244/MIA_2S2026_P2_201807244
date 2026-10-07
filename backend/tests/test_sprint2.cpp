#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include "structs.h"

using namespace std;

static string fixed_name(const char* b, int n) {
    int len = 0;
    while (len < n && b[len] != '\0') len++;
    return string(b, len);
}

static bool find_partition(FILE* f, const string& name, Partition& part) {
    MBR mbr{};
    fseek(f, 0, SEEK_SET);
    if (fread(&mbr, sizeof(MBR), 1, f) != 1) return false;
    for (int i = 0; i < 4; ++i) {
        if (mbr.mbr_partitions[i].part_s > 0 &&
            fixed_name(mbr.mbr_partitions[i].part_name, 16) == name) {
            part = mbr.mbr_partitions[i];
            return true;
        }
    }
    return false;
}

static string perm3(const char p[3]) {
    return string(p, p + 3);
}

static bool read_inode(FILE* f, const Superblock& sb, int idx, Inode& inode) {
    long pos = sb.s_inode_start + idx * (long)sizeof(Inode);
    fseek(f, pos, SEEK_SET);
    return fread(&inode, sizeof(Inode), 1, f) == 1;
}

static bool collect_pointer_data(FILE* f, const Superblock& sb, int block, int depth, vector<int>& out) {
    if (block < 0 || block >= sb.s_blocks_count) return false;
    if (depth == 0) {
        out.push_back(block);
        return true;
    }
    PointerBlock pb{};
    long pos = sb.s_block_start + block * 64L;
    fseek(f, pos, SEEK_SET);
    if (fread(&pb, sizeof(PointerBlock), 1, f) != 1) return false;
    for (int child : pb.b_pointers) {
        if (child == -1) continue;
        if (!collect_pointer_data(f, sb, child, depth - 1, out)) return false;
    }
    return true;
}

static string read_users_content(FILE* f, const Superblock& sb) {
    Inode inode{};
    if (!read_inode(f, sb, 1, inode)) return "";

    vector<int> blocks;
    for (int i = 0; i < 12; ++i) {
        if (inode.i_block[i] >= 0) blocks.push_back(inode.i_block[i]);
    }
    if (inode.i_block[12] >= 0) collect_pointer_data(f, sb, inode.i_block[12], 1, blocks);
    if (inode.i_block[13] >= 0) collect_pointer_data(f, sb, inode.i_block[13], 2, blocks);
    if (inode.i_block[14] >= 0) collect_pointer_data(f, sb, inode.i_block[14], 3, blocks);

    string out;
    int remaining = inode.i_s;
    for (int block : blocks) {
        if (remaining <= 0) break;
        FileBlock b{};
        long pos = sb.s_block_start + block * 64L;
        fseek(f, pos, SEEK_SET);
        if (fread(&b, sizeof(FileBlock), 1, f) != 1) break;
        int take = min(64, remaining);
        out.append(b.b_content, b.b_content + take);
        remaining -= take;
    }
    return out;
}

static void inspect(const string& path, const string& name) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { cout << "ERROR=open" << endl; return; }

    Partition p;
    if (!find_partition(f, name, p)) {
        cout << "ERROR=partition" << endl;
        fclose(f);
        return;
    }

    Superblock sb{};
    fseek(f, p.part_start, SEEK_SET);
    if (fread(&sb, sizeof(Superblock), 1, f) != 1) {
        cout << "ERROR=superblock" << endl;
        fclose(f);
        return;
    }

    cout << "part_start=" << p.part_start << '\n';
    cout << "part_s=" << p.part_s << '\n';
    cout << "fs_type=" << sb.s_filesystem_type << '\n';
    cout << "inodes_count=" << sb.s_inodes_count << '\n';
    cout << "blocks_count=" << sb.s_blocks_count << '\n';
    cout << "free_inodes=" << sb.s_free_inodes_count << '\n';
    cout << "free_blocks=" << sb.s_free_blocks_count << '\n';
    cout << "magic=" << sb.s_magic << '\n';
    cout << "inode_s=" << sb.s_inode_s << '\n';
    cout << "block_s=" << sb.s_block_s << '\n';
    cout << "first_ino=" << sb.s_firts_ino << '\n';
    cout << "first_blo=" << sb.s_first_blo << '\n';
    cout << "bm_inode_start=" << sb.s_bm_inode_start << '\n';
    cout << "bm_block_start=" << sb.s_bm_block_start << '\n';
    cout << "inode_start=" << sb.s_inode_start << '\n';
    cout << "block_start=" << sb.s_block_start << '\n';
    cout << "mnt_count=" << sb.s_mnt_count << '\n';

    string bmi, bmb;
    for (int i = 0; i < min(8, sb.s_inodes_count); ++i) {
        char c = 0;
        fseek(f, sb.s_bm_inode_start + i, SEEK_SET);
        fread(&c, 1, 1, f);
        bmi += c;
    }
    for (int i = 0; i < min(8, sb.s_blocks_count); ++i) {
        char c = 0;
        fseek(f, sb.s_bm_block_start + i, SEEK_SET);
        fread(&c, 1, 1, f);
        bmb += c;
    }
    cout << "bm_inode_prefix=" << bmi << '\n';
    cout << "bm_block_prefix=" << bmb << '\n';

    Inode root{}, users{};
    read_inode(f, sb, 0, root);
    read_inode(f, sb, 1, users);
    cout << "root_uid=" << root.i_uid << '\n';
    cout << "root_gid=" << root.i_gid << '\n';
    cout << "root_type=" << root.i_type << '\n';
    cout << "root_perm=" << perm3(root.i_perm) << '\n';
    cout << "root_block0=" << root.i_block[0] << '\n';
    cout << "users_uid=" << users.i_uid << '\n';
    cout << "users_gid=" << users.i_gid << '\n';
    cout << "users_type=" << users.i_type << '\n';
    cout << "users_perm=" << perm3(users.i_perm) << '\n';
    cout << "users_size=" << users.i_s << '\n';
    cout << "users_block0=" << users.i_block[0] << '\n';
    cout << "users_block12=" << users.i_block[12] << '\n';
    cout << "users_block13=" << users.i_block[13] << '\n';

    FolderBlock folder{};
    fseek(f, sb.s_block_start + root.i_block[0] * 64L, SEEK_SET);
    fread(&folder, sizeof(FolderBlock), 1, f);
    for (int i = 0; i < 4; ++i) {
        cout << "root_entry" << i << "_name=" << fixed_name(folder.b_content[i].b_name, 12) << '\n';
        cout << "root_entry" << i << "_inode=" << folder.b_content[i].b_inodo << '\n';
    }

    fclose(f);
}

static void users(const string& path, const string& name) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;
    Partition p;
    if (!find_partition(f, name, p)) { fclose(f); return; }
    Superblock sb{};
    fseek(f, p.part_start, SEEK_SET);
    if (fread(&sb, sizeof(Superblock), 1, f) != 1) { fclose(f); return; }
    cout << read_users_content(f, sb);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    string cmd = argv[1];
    if (cmd == "sizes") {
        cout << "SB=" << sizeof(Superblock) << '\n';
        cout << "INODE=" << sizeof(Inode) << '\n';
        cout << "BLOCK=" << sizeof(FileBlock) << '\n';
        return 0;
    }
    if ((cmd == "inspect" || cmd == "users") && argc >= 4) {
        if (cmd == "inspect") inspect(argv[2], argv[3]);
        else users(argv[2], argv[3]);
        return 0;
    }
    return 1;
}
