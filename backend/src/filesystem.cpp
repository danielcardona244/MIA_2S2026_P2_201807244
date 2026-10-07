#include "filesystem.h"

#include "disk.h"
#include "mount.h"
#include "shared.h"
#include "users.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <set>
#include <functional>
#include <cctype>

using namespace std;

namespace {

constexpr int BLOCK_SIZE = 64;
constexpr int ROOT_INODE_INDEX = 0;
constexpr int USERS_INODE_INDEX = 1;
constexpr int ROOT_BLOCK_INDEX = 0;
constexpr int USERS_BLOCK_INDEX = 1;

static_assert(sizeof(FolderBlock) == BLOCK_SIZE, "FolderBlock debe medir 64 bytes");
static_assert(sizeof(FileBlock) == BLOCK_SIZE, "FileBlock debe medir 64 bytes");
static_assert(sizeof(PointerBlock) == BLOCK_SIZE, "PointerBlock debe medir 64 bytes");

bool io_seek(FILE* file, long long offset) {
    if (offset < 0 || offset > LONG_MAX) return false;
    return fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
}

bool read_exact(FILE* file, long long offset, void* out, size_t size) {
    if (!io_seek(file, offset)) return false;
    return fread(out, 1, size, file) == size;
}

bool write_exact(FILE* file, long long offset, const void* data, size_t size) {
    if (!io_seek(file, offset)) return false;
    return fwrite(data, 1, size, file) == size;
}

bool find_primary_partition(const string& path,
                            const string& name,
                            Partition& out_partition,
                            int& out_index,
                            string& error) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        error = "no se pudo abrir el disco";
        return false;
    }

    MBR mbr{};
    bool ok = fread(&mbr, sizeof(MBR), 1, file) == 1;
    fclose(file);
    if (!ok) {
        error = "no se pudo leer el MBR";
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        const Partition& p = mbr.mbr_partitions[i];
        if (p.part_s > 0 && p.part_start >= 0 && p.part_type == 'P' &&
            compare_ci(read_fixed(p.part_name, 16), name)) {
            out_partition = p;
            out_index = i;
            return true;
        }
    }

    error = "la particion montada ya no existe o no es primaria";
    return false;
}

bool resolve_mount(const string& id,
                   string& out_path,
                   Partition& out_partition,
                   int& out_index,
                   string& error) {
    string name;
    if (!get_mount_info(id, out_path, name)) {
        error = "el id no corresponde a una particion montada";
        return false;
    }
    return find_primary_partition(out_path, name, out_partition, out_index, error);
}

bool wipe_partition(FILE* file, const Partition& part) {
    if (part.part_start < 0 || part.part_s <= 0) return false;

    char zeros[4096] = {0};
    long long remaining = part.part_s;
    long long pos = part.part_start;

    while (remaining > 0) {
        size_t chunk = static_cast<size_t>(min<long long>(remaining, sizeof(zeros)));
        if (!write_exact(file, pos, zeros, chunk)) return false;
        pos += static_cast<long long>(chunk);
        remaining -= static_cast<long long>(chunk);
    }
    return true;
}

void initialize_inode(Inode& inode,
                      int uid,
                      int gid,
                      int size,
                      char type,
                      const char perm[3],
                      int first_block,
                      time_t now) {
    inode = Inode();
    inode.i_uid = uid;
    inode.i_gid = gid;
    inode.i_s = size;
    inode.i_atime = now;
    inode.i_ctime = now;
    inode.i_mtime = now;
    for (int& block : inode.i_block) block = -1;
    inode.i_block[0] = first_block;
    inode.i_type = type;
    memcpy(inode.i_perm, perm, 3);
}

int first_free_bitmap(FILE* file, int bitmap_start, int count) {
    if (count <= 0) return -1;
    if (!io_seek(file, bitmap_start)) return -1;
    for (int i = 0; i < count; ++i) {
        char bit = 0;
        if (fread(&bit, 1, 1, file) != 1) return -1;
        if (bit == '0' || bit == 0) return i;
    }
    return -1;
}

bool write_superblock(FILE* file, int partition_start, const Superblock& sb) {
    return write_exact(file, partition_start, &sb, sizeof(Superblock));
}

bool read_inode(FILE* file, const Superblock& sb, int index, Inode& inode) {
    if (index < 0 || index >= sb.s_inodes_count) return false;
    long long pos = static_cast<long long>(sb.s_inode_start) +
                    static_cast<long long>(index) * sizeof(Inode);
    return read_exact(file, pos, &inode, sizeof(Inode));
}

bool write_inode(FILE* file, const Superblock& sb, int index, const Inode& inode) {
    if (index < 0 || index >= sb.s_inodes_count) return false;
    long long pos = static_cast<long long>(sb.s_inode_start) +
                    static_cast<long long>(index) * sizeof(Inode);
    return write_exact(file, pos, &inode, sizeof(Inode));
}

bool read_file_block(FILE* file, const Superblock& sb, int index, FileBlock& block) {
    if (index < 0 || index >= sb.s_blocks_count) return false;
    long long pos = static_cast<long long>(sb.s_block_start) +
                    static_cast<long long>(index) * BLOCK_SIZE;
    return read_exact(file, pos, &block, sizeof(FileBlock));
}

bool write_file_block(FILE* file, const Superblock& sb, int index, const FileBlock& block) {
    if (index < 0 || index >= sb.s_blocks_count) return false;
    long long pos = static_cast<long long>(sb.s_block_start) +
                    static_cast<long long>(index) * BLOCK_SIZE;
    return write_exact(file, pos, &block, sizeof(FileBlock));
}

bool write_folder_block(FILE* file, const Superblock& sb, int index, const FolderBlock& block) {
    if (index < 0 || index >= sb.s_blocks_count) return false;
    long long pos = static_cast<long long>(sb.s_block_start) +
                    static_cast<long long>(index) * BLOCK_SIZE;
    return write_exact(file, pos, &block, sizeof(FolderBlock));
}

bool set_bitmap(FILE* file, int start, int count, int index, char value) {
    if (index < 0 || index >= count) return false;
    return write_exact(file, static_cast<long long>(start) + index, &value, 1);
}

int allocate_block(FILE* file, Superblock& sb) {
    int index = first_free_bitmap(file, sb.s_bm_block_start, sb.s_blocks_count);
    if (index < 0) return -1;

    if (!set_bitmap(file, sb.s_bm_block_start, sb.s_blocks_count, index, '1')) return -1;
    if (sb.s_free_blocks_count > 0) sb.s_free_blocks_count--;

    int next = first_free_bitmap(file, sb.s_bm_block_start, sb.s_blocks_count);
    sb.s_first_blo = (next >= 0)
        ? sb.s_block_start + next * BLOCK_SIZE
        : -1;
    return index;
}

bool read_pointer_block(FILE* file, const Superblock& sb, int index, PointerBlock& block) {
    if (index < 0 || index >= sb.s_blocks_count) return false;
    long long pos = static_cast<long long>(sb.s_block_start) +
                    static_cast<long long>(index) * BLOCK_SIZE;
    return read_exact(file, pos, &block, sizeof(PointerBlock));
}

bool write_pointer_block(FILE* file, const Superblock& sb, int index, const PointerBlock& block) {
    if (index < 0 || index >= sb.s_blocks_count) return false;
    long long pos = static_cast<long long>(sb.s_block_start) +
                    static_cast<long long>(index) * BLOCK_SIZE;
    return write_exact(file, pos, &block, sizeof(PointerBlock));
}

bool valid_block(const Superblock& sb, int block) {
    return block >= 0 && block < sb.s_blocks_count;
}

bool collect_single(FILE* file,
                    const Superblock& sb,
                    int pointer_index,
                    vector<int>& data_blocks,
                    vector<int>& pointer_blocks,
                    string& error) {
    if (!valid_block(sb, pointer_index)) {
        error = "apuntador simple fuera de rango";
        return false;
    }
    PointerBlock pb{};
    if (!read_pointer_block(file, sb, pointer_index, pb)) {
        error = "no se pudo leer bloque apuntador simple";
        return false;
    }
    pointer_blocks.push_back(pointer_index);
    for (int data : pb.b_pointers) {
        if (data == -1) continue;
        if (!valid_block(sb, data)) {
            error = "bloque de datos fuera de rango";
            return false;
        }
        data_blocks.push_back(data);
    }
    return true;
}

bool collect_double(FILE* file,
                    const Superblock& sb,
                    int pointer_index,
                    vector<int>& data_blocks,
                    vector<int>& pointer_blocks,
                    string& error) {
    if (!valid_block(sb, pointer_index)) {
        error = "apuntador doble fuera de rango";
        return false;
    }
    PointerBlock top{};
    if (!read_pointer_block(file, sb, pointer_index, top)) {
        error = "no se pudo leer bloque apuntador doble";
        return false;
    }
    pointer_blocks.push_back(pointer_index);
    for (int child : top.b_pointers) {
        if (child == -1) continue;
        if (!collect_single(file, sb, child, data_blocks, pointer_blocks, error)) return false;
    }
    return true;
}

bool collect_triple(FILE* file,
                    const Superblock& sb,
                    int pointer_index,
                    vector<int>& data_blocks,
                    vector<int>& pointer_blocks,
                    string& error) {
    if (!valid_block(sb, pointer_index)) {
        error = "apuntador triple fuera de rango";
        return false;
    }
    PointerBlock top{};
    if (!read_pointer_block(file, sb, pointer_index, top)) {
        error = "no se pudo leer bloque apuntador triple";
        return false;
    }
    pointer_blocks.push_back(pointer_index);
    for (int child : top.b_pointers) {
        if (child == -1) continue;
        if (!valid_block(sb, child)) {
            error = "apuntador de segundo nivel fuera de rango";
            return false;
        }
        PointerBlock middle{};
        if (!read_pointer_block(file, sb, child, middle)) {
            error = "no se pudo leer apuntador de segundo nivel";
            return false;
        }
        pointer_blocks.push_back(child);
        for (int grandchild : middle.b_pointers) {
            if (grandchild == -1) continue;
            if (!collect_single(file, sb, grandchild, data_blocks, pointer_blocks, error)) return false;
        }
    }
    return true;
}

bool collect_inode_blocks(FILE* file,
                          const Superblock& sb,
                          const Inode& inode,
                          vector<int>& data_blocks,
                          vector<int>& pointer_blocks,
                          string& error) {
    data_blocks.clear();
    pointer_blocks.clear();

    for (int i = 0; i < 12; ++i) {
        if (inode.i_block[i] == -1) continue;
        if (!valid_block(sb, inode.i_block[i])) {
            error = "bloque directo fuera de rango";
            return false;
        }
        data_blocks.push_back(inode.i_block[i]);
    }
    if (inode.i_block[12] != -1 &&
        !collect_single(file, sb, inode.i_block[12], data_blocks, pointer_blocks, error)) return false;
    if (inode.i_block[13] != -1 &&
        !collect_double(file, sb, inode.i_block[13], data_blocks, pointer_blocks, error)) return false;
    if (inode.i_block[14] != -1 &&
        !collect_triple(file, sb, inode.i_block[14], data_blocks, pointer_blocks, error)) return false;
    return true;
}

int pointer_blocks_needed(int data_count) {
    if (data_count <= 12) return 0;
    int remaining = data_count - 12;
    int total = 0;

    int single_data = min(remaining, 16);
    if (single_data > 0) {
        total += 1;
        remaining -= single_data;
    }

    int double_data = min(remaining, 16 * 16);
    if (double_data > 0) {
        total += 1; // bloque superior doble
        total += (double_data + 15) / 16; // bloques simples hijos
        remaining -= double_data;
    }

    if (remaining > 0) {
        int triple_data = min(remaining, 16 * 16 * 16);
        total += 1; // nivel superior triple
        total += (triple_data + 255) / 256; // nivel medio
        total += (triple_data + 15) / 16;   // nivel simple
        remaining -= triple_data;
    }

    return remaining == 0 ? total : -1;
}

bool mark_free_without_rescan(FILE* file, Superblock& sb, int index) {
    if (!valid_block(sb, index)) return false;
    char current = 0;
    if (!read_exact(file, static_cast<long long>(sb.s_bm_block_start) + index, &current, 1)) return false;
    if (current == '1') {
        if (!set_bitmap(file, sb.s_bm_block_start, sb.s_blocks_count, index, '0')) return false;
        sb.s_free_blocks_count++;
    }
    return true;
}

bool read_inode_file(FILE* file,
                     const Superblock& sb,
                     const Inode& inode,
                     string& content,
                     string& error) {
    if (inode.i_s < 0) {
        error = "tamano de archivo invalido";
        return false;
    }

    vector<int> data_blocks, pointer_blocks;
    if (!collect_inode_blocks(file, sb, inode, data_blocks, pointer_blocks, error)) return false;

    content.clear();
    int remaining = inode.i_s;
    for (int block_index : data_blocks) {
        if (remaining <= 0) break;
        FileBlock block{};
        if (!read_file_block(file, sb, block_index, block)) {
            error = "no se pudo leer un bloque de archivo";
            return false;
        }
        int take = min(BLOCK_SIZE, remaining);
        content.append(block.b_content, block.b_content + take);
        remaining -= take;
    }

    if (remaining != 0) {
        error = "el inodo no contiene suficientes bloques para su tamano";
        return false;
    }
    return true;
}

bool allocate_data_block(FILE* file, Superblock& sb, int& out, string& error) {
    out = allocate_block(file, sb);
    if (out < 0) {
        error = "no hay bloques libres";
        return false;
    }
    return true;
}

bool build_single(FILE* file,
                  Superblock& sb,
                  const vector<int>& data,
                  size_t& cursor,
                  int max_data,
                  int& out_pointer,
                  string& error) {
    if (!allocate_data_block(file, sb, out_pointer, error)) return false;
    PointerBlock pb{};
    for (int& p : pb.b_pointers) p = -1;
    for (int i = 0; i < 16 && i < max_data && cursor < data.size(); ++i) {
        pb.b_pointers[i] = data[cursor++];
    }
    if (!write_pointer_block(file, sb, out_pointer, pb)) {
        error = "no se pudo escribir bloque apuntador";
        return false;
    }
    return true;
}

bool rebuild_inode_block_map(FILE* file,
                             Superblock& sb,
                             Inode& inode,
                             const vector<int>& data,
                             string& error) {
    for (int& block : inode.i_block) block = -1;
    size_t cursor = 0;

    for (int i = 0; i < 12 && cursor < data.size(); ++i) {
        inode.i_block[i] = data[cursor++];
    }

    if (cursor < data.size()) {
        if (!build_single(file, sb, data, cursor, 16, inode.i_block[12], error)) return false;
    }

    if (cursor < data.size()) {
        if (!allocate_data_block(file, sb, inode.i_block[13], error)) return false;
        PointerBlock top{};
        for (int& p : top.b_pointers) p = -1;
        for (int i = 0; i < 16 && cursor < data.size(); ++i) {
            int child = -1;
            if (!build_single(file, sb, data, cursor, 16, child, error)) return false;
            top.b_pointers[i] = child;
        }
        if (!write_pointer_block(file, sb, inode.i_block[13], top)) {
            error = "no se pudo escribir apuntador doble";
            return false;
        }
    }

    if (cursor < data.size()) {
        if (!allocate_data_block(file, sb, inode.i_block[14], error)) return false;
        PointerBlock top{};
        for (int& p : top.b_pointers) p = -1;
        for (int i = 0; i < 16 && cursor < data.size(); ++i) {
            int middle_index = -1;
            if (!allocate_data_block(file, sb, middle_index, error)) return false;
            top.b_pointers[i] = middle_index;

            PointerBlock middle{};
            for (int& p : middle.b_pointers) p = -1;
            for (int j = 0; j < 16 && cursor < data.size(); ++j) {
                int child = -1;
                if (!build_single(file, sb, data, cursor, 16, child, error)) return false;
                middle.b_pointers[j] = child;
            }
            if (!write_pointer_block(file, sb, middle_index, middle)) {
                error = "no se pudo escribir apuntador triple intermedio";
                return false;
            }
        }
        if (!write_pointer_block(file, sb, inode.i_block[14], top)) {
            error = "no se pudo escribir apuntador triple";
            return false;
        }
    }

    if (cursor != data.size()) {
        error = "archivo excede la capacidad maxima del inodo";
        return false;
    }
    return true;
}

bool write_inode_file(FILE* file,
                      int partition_start,
                      Superblock& sb,
                      int inode_index,
                      Inode& inode,
                      const string& content,
                      string& error) {
    int needed_data = max(1, static_cast<int>((content.size() + BLOCK_SIZE - 1) / BLOCK_SIZE));
    const int max_data = 12 + 16 + 16 * 16 + 16 * 16 * 16;
    if (needed_data > max_data) {
        error = "archivo excede la capacidad maxima de apuntadores del inodo";
        return false;
    }

    vector<int> old_data, old_pointers;
    if (!collect_inode_blocks(file, sb, inode, old_data, old_pointers, error)) return false;

    int p_needed = pointer_blocks_needed(needed_data);
    if (p_needed < 0) {
        error = "no se pudo calcular la estructura de apuntadores";
        return false;
    }
    int total_needed = needed_data + p_needed;
    int reusable = static_cast<int>(old_data.size() + old_pointers.size());
    if (total_needed > sb.s_free_blocks_count + reusable) {
        error = "no hay bloques libres suficientes";
        return false;
    }

    // Liberar el mapa anterior y reconstruirlo de forma determinista. El bitmap es la
    // fuente de verdad, por lo que no se toca ningun bloque ajeno al inodo.
    for (int b : old_data) {
        if (!mark_free_without_rescan(file, sb, b)) {
            error = "no se pudo liberar un bloque de datos previo";
            return false;
        }
    }
    for (int b : old_pointers) {
        if (!mark_free_without_rescan(file, sb, b)) {
            error = "no se pudo liberar un bloque apuntador previo";
            return false;
        }
    }
    int first = first_free_bitmap(file, sb.s_bm_block_start, sb.s_blocks_count);
    sb.s_first_blo = (first >= 0) ? sb.s_block_start + first * BLOCK_SIZE : -1;

    vector<int> data_blocks;
    data_blocks.reserve(needed_data);
    for (int i = 0; i < needed_data; ++i) {
        int block = -1;
        if (!allocate_data_block(file, sb, block, error)) return false;
        data_blocks.push_back(block);
    }

    if (!rebuild_inode_block_map(file, sb, inode, data_blocks, error)) return false;

    for (int i = 0; i < needed_data; ++i) {
        FileBlock block{};
        size_t offset = static_cast<size_t>(i) * BLOCK_SIZE;
        size_t count = min<size_t>(BLOCK_SIZE, content.size() > offset ? content.size() - offset : 0);
        if (count > 0) memcpy(block.b_content, content.data() + offset, count);
        if (!write_file_block(file, sb, data_blocks[i], block)) {
            error = "no se pudo escribir un bloque de archivo";
            return false;
        }
    }

    inode.i_s = static_cast<int>(content.size());
    inode.i_mtime = time(nullptr);
    if (!write_inode(file, sb, inode_index, inode)) {
        error = "no se pudo actualizar el inodo";
        return false;
    }
    if (!write_superblock(file, partition_start, sb)) {
        error = "no se pudo actualizar el superbloque";
        return false;
    }
    return true;
}

} // namespace

bool fs_load_context(const string& id,
                     string& out_path,
                     Partition& out_partition,
                     Superblock& out_superblock,
                     string& error) {
    int part_index = -1;
    if (!resolve_mount(id, out_path, out_partition, part_index, error)) return false;

    FILE* file = fopen(out_path.c_str(), "rb");
    if (!file) {
        error = "no se pudo abrir el disco";
        return false;
    }

    bool ok = read_exact(file, out_partition.part_start, &out_superblock, sizeof(Superblock));
    fclose(file);
    if (!ok) {
        error = "no se pudo leer el superbloque";
        return false;
    }

    if (out_superblock.s_magic != 0xEF53 || out_superblock.s_filesystem_type != 2) {
        error = "la particion no tiene un sistema de archivos EXT2 valido";
        return false;
    }
    return true;
}

void cmd_mkfs(map<string, string>& params) {
    if (params.find("id") == params.end()) {
        cout << "[ERROR] MKFS: falta parametro -id" << endl;
        return;
    }

    string id = params["id"];
    if (id.empty()) {
        cout << "[ERROR] MKFS: id no puede estar vacio" << endl;
        return;
    }

    string type = "full";
    if (params.find("type") != params.end()) type = to_lower(params["type"]);
    if (type != "full") {
        cout << "[ERROR] MKFS: -type solo admite full" << endl;
        return;
    }

    string path;
    Partition part;
    int part_index = -1;
    string error;
    if (!resolve_mount(id, path, part, part_index, error)) {
        cout << "[ERROR] MKFS: " << error << endl;
        return;
    }

    long long denominator = 1LL + 3LL + sizeof(Inode) + 3LL * BLOCK_SIZE;
    long long numerator = static_cast<long long>(part.part_s) - sizeof(Superblock);
    if (numerator <= 0) {
        cout << "[ERROR] MKFS: la particion es demasiado pequena" << endl;
        return;
    }

    long long n_ll = numerator / denominator;
    if (n_ll < 2 || n_ll > INT_MAX / 3) {
        cout << "[ERROR] MKFS: la particion no permite crear las estructuras minimas" << endl;
        return;
    }
    int n = static_cast<int>(n_ll);

    long long bm_inode_start = static_cast<long long>(part.part_start) + sizeof(Superblock);
    long long bm_block_start = bm_inode_start + n;
    long long inode_start = bm_block_start + 3LL * n;
    long long block_start = inode_start + static_cast<long long>(n) * sizeof(Inode);
    long long layout_end = block_start + static_cast<long long>(3) * n * BLOCK_SIZE;
    long long partition_end = static_cast<long long>(part.part_start) + part.part_s;

    if (layout_end > partition_end || block_start > INT_MAX) {
        cout << "[ERROR] MKFS: el layout EXT2 excede la particion" << endl;
        return;
    }

    FILE* file = fopen(path.c_str(), "r+b");
    if (!file) {
        cout << "[ERROR] MKFS: no se pudo abrir el disco" << endl;
        return;
    }

    if (!wipe_partition(file, part)) {
        fclose(file);
        cout << "[ERROR] MKFS: fallo el formateo completo de la particion" << endl;
        return;
    }

    const time_t now = time(nullptr);
    Superblock sb{};
    sb.s_filesystem_type = 2;
    sb.s_inodes_count = n;
    sb.s_blocks_count = 3 * n;
    sb.s_free_inodes_count = n - 2;
    sb.s_free_blocks_count = 3 * n - 2;
    sb.s_mtime = now;
    sb.s_umtime = 0;
    sb.s_mnt_count = 1;
    sb.s_magic = 0xEF53;
    sb.s_inode_s = sizeof(Inode);
    sb.s_block_s = BLOCK_SIZE;
    sb.s_bm_inode_start = static_cast<int>(bm_inode_start);
    sb.s_bm_block_start = static_cast<int>(bm_block_start);
    sb.s_inode_start = static_cast<int>(inode_start);
    sb.s_block_start = static_cast<int>(block_start);
    sb.s_firts_ino = (n > 2) ? sb.s_inode_start + 2 * static_cast<int>(sizeof(Inode)) : -1;
    sb.s_first_blo = (sb.s_blocks_count > 2) ? sb.s_block_start + 2 * BLOCK_SIZE : -1;

    if (!write_superblock(file, part.part_start, sb)) {
        fclose(file);
        cout << "[ERROR] MKFS: no se pudo escribir el superbloque" << endl;
        return;
    }

    vector<char> inode_bitmap(n, '0');
    vector<char> block_bitmap(3 * n, '0');
    inode_bitmap[ROOT_INODE_INDEX] = '1';
    inode_bitmap[USERS_INODE_INDEX] = '1';
    block_bitmap[ROOT_BLOCK_INDEX] = '1';
    block_bitmap[USERS_BLOCK_INDEX] = '1';

    if (!write_exact(file, sb.s_bm_inode_start, inode_bitmap.data(), inode_bitmap.size()) ||
        !write_exact(file, sb.s_bm_block_start, block_bitmap.data(), block_bitmap.size())) {
        fclose(file);
        cout << "[ERROR] MKFS: no se pudieron escribir los bitmaps" << endl;
        return;
    }

    const string initial_users = "1,G,root\n1,U,root,root,123\n";
    const char perm664[3] = {'6', '6', '4'};

    Inode root_inode;
    initialize_inode(root_inode, 1, 1, sizeof(FolderBlock), '0', perm664, ROOT_BLOCK_INDEX, now);

    Inode users_inode;
    initialize_inode(users_inode, 1, 1, static_cast<int>(initial_users.size()), '1', perm664,
                     USERS_BLOCK_INDEX, now);

    if (!write_inode(file, sb, ROOT_INODE_INDEX, root_inode) ||
        !write_inode(file, sb, USERS_INODE_INDEX, users_inode)) {
        fclose(file);
        cout << "[ERROR] MKFS: no se pudieron escribir los inodos iniciales" << endl;
        return;
    }

    FolderBlock root_block{};
    write_fixed(root_block.b_content[0].b_name, 12, ".");
    root_block.b_content[0].b_inodo = ROOT_INODE_INDEX;
    write_fixed(root_block.b_content[1].b_name, 12, "..");
    root_block.b_content[1].b_inodo = ROOT_INODE_INDEX;
    write_fixed(root_block.b_content[2].b_name, 12, "users.txt");
    root_block.b_content[2].b_inodo = USERS_INODE_INDEX;

    FileBlock users_block{};
    memcpy(users_block.b_content, initial_users.data(), initial_users.size());

    if (!write_folder_block(file, sb, ROOT_BLOCK_INDEX, root_block) ||
        !write_file_block(file, sb, USERS_BLOCK_INDEX, users_block)) {
        fclose(file);
        cout << "[ERROR] MKFS: no se pudieron escribir los bloques iniciales" << endl;
        return;
    }

    fflush(file);
    fclose(file);

    cout << "[OK] MKFS: particion " << id << " formateada como EXT2"
         << " (inodos=" << n << ", bloques=" << (3 * n) << ")" << endl;
}

bool fs_read_users_file(const string& id, string& content, string& error) {
    string path;
    Partition part;
    Superblock sb;
    if (!fs_load_context(id, path, part, sb, error)) return false;

    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        error = "no se pudo abrir el disco";
        return false;
    }

    Inode users_inode;
    if (!read_inode(file, sb, USERS_INODE_INDEX, users_inode)) {
        fclose(file);
        error = "no se pudo leer el inodo de users.txt";
        return false;
    }
    if (users_inode.i_type != '1') {
        fclose(file);
        error = "el inodo de users.txt no es un archivo";
        return false;
    }

    bool ok = read_inode_file(file, sb, users_inode, content, error);
    fclose(file);
    return ok;
}

bool fs_write_users_file(const string& id, const string& content, string& error) {
    string path;
    Partition part;
    Superblock sb;
    if (!fs_load_context(id, path, part, sb, error)) return false;

    FILE* file = fopen(path.c_str(), "r+b");
    if (!file) {
        error = "no se pudo abrir el disco";
        return false;
    }

    Inode users_inode;
    if (!read_inode(file, sb, USERS_INODE_INDEX, users_inode)) {
        fclose(file);
        error = "no se pudo leer el inodo de users.txt";
        return false;
    }

    bool ok = write_inode_file(file, part.part_start, sb, USERS_INODE_INDEX,
                               users_inode, content, error);
    fflush(file);
    fclose(file);
    return ok;
}



// ================================================================
// Sprint 4 - Archivos, carpetas y permisos
// ================================================================

namespace {

const char DEFAULT_PERM_664[3] = {'6', '6', '4'};

bool require_fs_session(const string& command) {
    if (!current_session.active) {
        cout << "[ERROR] " << command << ": necesita iniciar sesion" << endl;
        return false;
    }
    return true;
}

bool parse_absolute_path(const string& raw, vector<string>& parts, string& error) {
    parts.clear();
    if (raw.empty() || raw[0] != '/') {
        error = "la ruta debe ser absoluta y comenzar con /";
        return false;
    }

    string current;
    for (size_t i = 1; i <= raw.size(); ++i) {
        char c = (i < raw.size()) ? raw[i] : '/';
        if (c == '/') {
            if (!current.empty()) {
                if (current == "." || current == "..") {
                    error = "no se admiten componentes . o .. en la ruta";
                    return false;
                }
                if (current.size() > 12) {
                    error = "cada nombre de archivo o carpeta admite maximo 12 caracteres";
                    return false;
                }
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    return true;
}

bool permission_digit(const Inode& inode, int& digit) {
    int pos = 2;
    if (current_session.uid == inode.i_uid) pos = 0;
    else if (current_session.gid == inode.i_gid) pos = 1;

    char c = inode.i_perm[pos];
    if (c < '0' || c > '7') return false;
    digit = c - '0';
    return true;
}

bool has_permission(const Inode& inode, int mask) {
    // El enunciado establece que root posee 777 sobre cualquier objeto.
    if (current_session.uid == 1 && current_session.user == "root") return true;
    int digit = 0;
    return permission_digit(inode, digit) && (digit & mask) == mask;
}

int allocate_inode_index(FILE* file, Superblock& sb) {
    int index = first_free_bitmap(file, sb.s_bm_inode_start, sb.s_inodes_count);
    if (index < 0) return -1;
    if (!set_bitmap(file, sb.s_bm_inode_start, sb.s_inodes_count, index, '1')) return -1;
    if (sb.s_free_inodes_count > 0) sb.s_free_inodes_count--;

    int next = first_free_bitmap(file, sb.s_bm_inode_start, sb.s_inodes_count);
    sb.s_firts_ino = (next >= 0)
        ? sb.s_inode_start + next * static_cast<int>(sizeof(Inode))
        : -1;
    return index;
}

bool read_folder_block(FILE* file, const Superblock& sb, int index, FolderBlock& block) {
    if (index < 0 || index >= sb.s_blocks_count) return false;
    long long pos = static_cast<long long>(sb.s_block_start) +
                    static_cast<long long>(index) * BLOCK_SIZE;
    return read_exact(file, pos, &block, sizeof(FolderBlock));
}

bool inode_data_blocks(FILE* file,
                       const Superblock& sb,
                       const Inode& inode,
                       vector<int>& blocks,
                       string& error) {
    vector<int> pointer_blocks;
    return collect_inode_blocks(file, sb, inode, blocks, pointer_blocks, error);
}

bool find_child(FILE* file,
                const Superblock& sb,
                const Inode& directory,
                const string& name,
                int& child_index,
                string& error) {
    if (directory.i_type != '0') {
        error = "un componente de la ruta no es una carpeta";
        return false;
    }

    vector<int> blocks;
    if (!inode_data_blocks(file, sb, directory, blocks, error)) return false;

    for (int block_index : blocks) {
        FolderBlock folder{};
        if (!read_folder_block(file, sb, block_index, folder)) {
            error = "no se pudo leer un bloque de carpeta";
            return false;
        }
        for (const Content& entry : folder.b_content) {
            if (entry.b_inodo < 0) continue;
            if (read_fixed(entry.b_name, 12) == name) {
                child_index = entry.b_inodo;
                return true;
            }
        }
    }
    child_index = -1;
    return true;
}

bool resolve_inode_path(FILE* file,
                        const Superblock& sb,
                        const vector<string>& parts,
                        int& inode_index,
                        Inode& inode,
                        string& error) {
    inode_index = ROOT_INODE_INDEX;
    if (!read_inode(file, sb, inode_index, inode)) {
        error = "no se pudo leer el inodo raiz";
        return false;
    }

    for (const string& part : parts) {
        int child = -1;
        if (!find_child(file, sb, inode, part, child, error)) return false;
        if (child < 0) {
            error = "no existe la ruta solicitada";
            return false;
        }
        if (!read_inode(file, sb, child, inode)) {
            error = "no se pudo leer un inodo de la ruta";
            return false;
        }
        inode_index = child;
    }
    return true;
}

bool append_directory_entry(FILE* file,
                            int partition_start,
                            Superblock& sb,
                            int parent_index,
                            Inode& parent,
                            const string& name,
                            int child_index,
                            string& error) {
    vector<int> data_blocks, pointer_blocks;
    if (!collect_inode_blocks(file, sb, parent, data_blocks, pointer_blocks, error)) return false;

    // Primero reutilizar un espacio libre de los bloques actuales.
    for (int block_index : data_blocks) {
        FolderBlock folder{};
        if (!read_folder_block(file, sb, block_index, folder)) {
            error = "no se pudo leer un bloque de carpeta";
            return false;
        }
        for (Content& entry : folder.b_content) {
            if (entry.b_inodo == -1) {
                write_fixed(entry.b_name, 12, name);
                entry.b_inodo = child_index;
                if (!write_folder_block(file, sb, block_index, folder)) {
                    error = "no se pudo actualizar la carpeta padre";
                    return false;
                }
                parent.i_mtime = time(nullptr);
                return write_inode(file, sb, parent_index, parent);
            }
        }
    }

    // La carpeta necesita otro bloque de datos. Preservar sus bloques de datos y
    // reconstruir unicamente la estructura de apuntadores si ya fuera necesaria.
    int new_block = allocate_block(file, sb);
    if (new_block < 0) {
        error = "no hay bloques libres para ampliar la carpeta";
        return false;
    }

    FolderBlock folder{};
    write_fixed(folder.b_content[0].b_name, 12, name);
    folder.b_content[0].b_inodo = child_index;
    if (!write_folder_block(file, sb, new_block, folder)) {
        error = "no se pudo escribir el nuevo bloque de carpeta";
        return false;
    }

    for (int pointer : pointer_blocks) {
        if (!mark_free_without_rescan(file, sb, pointer)) {
            error = "no se pudo liberar un bloque apuntador previo";
            return false;
        }
    }

    data_blocks.push_back(new_block);
    if (!rebuild_inode_block_map(file, sb, parent, data_blocks, error)) return false;
    parent.i_s = static_cast<int>(data_blocks.size()) * BLOCK_SIZE;
    parent.i_mtime = time(nullptr);

    if (!write_inode(file, sb, parent_index, parent)) {
        error = "no se pudo actualizar el inodo de la carpeta padre";
        return false;
    }
    if (!write_superblock(file, partition_start, sb)) {
        error = "no se pudo actualizar el superbloque";
        return false;
    }
    return true;
}

bool create_directory_child(FILE* file,
                            int partition_start,
                            Superblock& sb,
                            int parent_index,
                            Inode& parent,
                            const string& name,
                            int& new_inode_index,
                            string& error) {
    if (!has_permission(parent, 2)) {
        error = "no tiene permiso de escritura en la carpeta padre";
        return false;
    }

    int existing = -1;
    if (!find_child(file, sb, parent, name, existing, error)) return false;
    if (existing >= 0) {
        error = "ya existe un archivo o carpeta con ese nombre";
        return false;
    }

    int inode_index = allocate_inode_index(file, sb);
    if (inode_index < 0) {
        error = "no hay inodos libres";
        return false;
    }
    int block_index = allocate_block(file, sb);
    if (block_index < 0) {
        error = "no hay bloques libres";
        return false;
    }

    Inode child;
    initialize_inode(child, current_session.uid, current_session.gid,
                     sizeof(FolderBlock), '0', DEFAULT_PERM_664, block_index, time(nullptr));

    FolderBlock folder{};
    write_fixed(folder.b_content[0].b_name, 12, ".");
    folder.b_content[0].b_inodo = inode_index;
    write_fixed(folder.b_content[1].b_name, 12, "..");
    folder.b_content[1].b_inodo = parent_index;

    if (!write_inode(file, sb, inode_index, child) ||
        !write_folder_block(file, sb, block_index, folder)) {
        error = "no se pudieron escribir las estructuras de la nueva carpeta";
        return false;
    }

    if (!append_directory_entry(file, partition_start, sb, parent_index,
                                parent, name, inode_index, error)) return false;

    if (!write_superblock(file, partition_start, sb)) {
        error = "no se pudo actualizar el superbloque";
        return false;
    }
    new_inode_index = inode_index;
    return true;
}

bool ensure_parent_directory(FILE* file,
                             int partition_start,
                             Superblock& sb,
                             const vector<string>& parent_parts,
                             bool create_missing,
                             int& parent_index,
                             Inode& parent,
                             string& error) {
    parent_index = ROOT_INODE_INDEX;
    if (!read_inode(file, sb, parent_index, parent)) {
        error = "no se pudo leer el inodo raiz";
        return false;
    }

    for (const string& part : parent_parts) {
        int child_index = -1;
        if (!find_child(file, sb, parent, part, child_index, error)) return false;

        if (child_index < 0) {
            if (!create_missing) {
                error = "no existen todas las carpetas padre";
                return false;
            }
            int created = -1;
            if (!create_directory_child(file, partition_start, sb, parent_index,
                                        parent, part, created, error)) return false;
            child_index = created;
        }

        Inode child{};
        if (!read_inode(file, sb, child_index, child)) {
            error = "no se pudo leer una carpeta padre";
            return false;
        }
        if (child.i_type != '0') {
            error = "un componente padre de la ruta no es una carpeta";
            return false;
        }
        parent_index = child_index;
        parent = child;
    }
    return true;
}

bool read_host_file(const string& path, string& content, string& error) {
    ifstream input(path, ios::binary);
    if (!input) {
        error = "el archivo indicado en -cont no existe o no se puede leer";
        return false;
    }
    ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        error = "no se pudo leer completamente el archivo indicado en -cont";
        return false;
    }
    content = buffer.str();
    return true;
}

string generated_content(int size) {
    string content;
    if (size <= 0) return content;
    content.resize(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i) content[static_cast<size_t>(i)] = static_cast<char>('0' + (i % 10));
    return content;
}

bool create_or_overwrite_file(FILE* file,
                              int partition_start,
                              Superblock& sb,
                              int parent_index,
                              Inode& parent,
                              const string& name,
                              const string& content,
                              string& error,
                              bool& overwritten) {
    overwritten = false;
    int existing = -1;
    if (!find_child(file, sb, parent, name, existing, error)) return false;

    if (existing >= 0) {
        Inode inode{};
        if (!read_inode(file, sb, existing, inode)) {
            error = "no se pudo leer el archivo existente";
            return false;
        }
        if (inode.i_type != '1') {
            error = "ya existe una carpeta con ese nombre";
            return false;
        }
        if (!has_permission(inode, 2)) {
            error = "no tiene permiso de escritura sobre el archivo existente";
            return false;
        }
        if (!write_inode_file(file, partition_start, sb, existing, inode, content, error)) return false;
        overwritten = true;
        return true;
    }

    if (!has_permission(parent, 2)) {
        error = "no tiene permiso de escritura en la carpeta padre";
        return false;
    }

    int inode_index = allocate_inode_index(file, sb);
    if (inode_index < 0) {
        error = "no hay inodos libres";
        return false;
    }

    Inode inode;
    initialize_inode(inode, current_session.uid, current_session.gid, 0, '1',
                     DEFAULT_PERM_664, -1, time(nullptr));

    if (!write_inode(file, sb, inode_index, inode)) {
        error = "no se pudo escribir el inodo del archivo";
        return false;
    }
    if (!write_inode_file(file, partition_start, sb, inode_index, inode, content, error)) return false;

    // write_inode_file actualiza el superbloque con la asignacion de bloques; la
    // copia local sb conserva esos cambios y se utiliza al insertar la entrada.
    if (!append_directory_entry(file, partition_start, sb, parent_index,
                                parent, name, inode_index, error)) return false;
    if (!write_superblock(file, partition_start, sb)) {
        error = "no se pudo actualizar el superbloque";
        return false;
    }
    return true;
}

struct UserIdentity {
    int uid = -1;
    int gid = -1;
    string user;
    string group;
};

vector<string> csv_fields(const string& line) {
    vector<string> out;
    string field;
    stringstream ss(line);
    while (getline(ss, field, ',')) out.push_back(trim(field));
    return out;
}

bool lookup_user_identity(const string& user, UserIdentity& identity, string& error) {
    string content;
    if (!fs_read_users_file(current_session.id, content, error)) return false;

    map<string, int> groups;
    struct Candidate { int uid; string group; string user; } candidate{-1, "", ""};

    string line;
    stringstream ss(content);
    while (getline(ss, line)) {
        vector<string> fields = csv_fields(line);
        if (fields.size() < 3) continue;
        int id = -1;
        if (!strict_stoi(fields[0], id) || id <= 0) continue;
        if (fields[1] == "G" && fields.size() >= 3) {
            groups[fields[2]] = id;
        } else if (fields[1] == "U" && fields.size() >= 5 && fields[3] == user) {
            candidate = {id, fields[2], fields[3]};
        }
    }

    if (candidate.uid < 0) {
        error = "el usuario indicado no existe";
        return false;
    }
    auto git = groups.find(candidate.group);
    if (git == groups.end()) {
        error = "el grupo del usuario indicado no existe";
        return false;
    }

    identity.uid = candidate.uid;
    identity.gid = git->second;
    identity.user = candidate.user;
    identity.group = candidate.group;
    return true;
}

bool collect_children(FILE* file,
                      const Superblock& sb,
                      const Inode& directory,
                      vector<int>& children,
                      string& error) {
    children.clear();
    if (directory.i_type != '0') return true;
    vector<int> blocks;
    if (!inode_data_blocks(file, sb, directory, blocks, error)) return false;
    for (int block_index : blocks) {
        FolderBlock folder{};
        if (!read_folder_block(file, sb, block_index, folder)) {
            error = "no se pudo leer un bloque de carpeta";
            return false;
        }
        for (const Content& entry : folder.b_content) {
            if (entry.b_inodo < 0) continue;
            string name = read_fixed(entry.b_name, 12);
            if (name == "." || name == "..") continue;
            children.push_back(entry.b_inodo);
        }
    }
    return true;
}

bool recurse_inode_tree(FILE* file,
                        const Superblock& sb,
                        int start_index,
                        const function<bool(int, Inode&, string&)>& action,
                        string& error) {
    set<int> visited;
    function<bool(int)> visit = [&](int index) -> bool {
        if (visited.count(index)) return true;
        visited.insert(index);

        Inode inode{};
        if (!read_inode(file, sb, index, inode)) {
            error = "no se pudo leer un inodo durante la operacion recursiva";
            return false;
        }
        if (!action(index, inode, error)) return false;

        if (inode.i_type == '0') {
            vector<int> children;
            if (!collect_children(file, sb, inode, children, error)) return false;
            for (int child : children) {
                if (!visit(child)) return false;
            }
        }
        return true;
    };
    return visit(start_index);
}

bool parse_ugo(const string& text, char out[3]) {
    if (text.size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        if (text[i] < '0' || text[i] > '7') return false;
        out[i] = text[i];
    }
    return true;
}

} // namespace

void cmd_mkdir(map<string, string>& params) {
    if (!require_fs_session("MKDIR")) return;
    auto it = params.find("path");
    if (it == params.end() || it->second.empty()) {
        cout << "[ERROR] MKDIR: falta parametro -path" << endl;
        return;
    }

    vector<string> parts;
    string error;
    if (!parse_absolute_path(it->second, parts, error) || parts.empty()) {
        cout << "[ERROR] MKDIR: " << (error.empty() ? "ruta invalida" : error) << endl;
        return;
    }
    bool recursive = params.count("p") > 0;

    string disk_path;
    Partition part;
    Superblock sb;
    if (!fs_load_context(current_session.id, disk_path, part, sb, error)) {
        cout << "[ERROR] MKDIR: " << error << endl;
        return;
    }

    FILE* file = fopen(disk_path.c_str(), "r+b");
    if (!file) {
        cout << "[ERROR] MKDIR: no se pudo abrir el disco" << endl;
        return;
    }

    string leaf = parts.back();
    parts.pop_back();
    int parent_index = -1;
    Inode parent{};
    if (!ensure_parent_directory(file, part.part_start, sb, parts, recursive,
                                 parent_index, parent, error)) {
        fclose(file);
        cout << "[ERROR] MKDIR: " << error << endl;
        return;
    }

    int existing = -1;
    if (!find_child(file, sb, parent, leaf, existing, error)) {
        fclose(file);
        cout << "[ERROR] MKDIR: " << error << endl;
        return;
    }
    if (existing >= 0) {
        fclose(file);
        if (recursive) cout << "[OK] MKDIR: la carpeta ya existe, no se realizaron cambios" << endl;
        else cout << "[ERROR] MKDIR: ya existe un archivo o carpeta con ese nombre" << endl;
        return;
    }

    int created = -1;
    bool ok = create_directory_child(file, part.part_start, sb, parent_index,
                                     parent, leaf, created, error);
    fflush(file);
    fclose(file);
    if (!ok) {
        cout << "[ERROR] MKDIR: " << error << endl;
        return;
    }
    cout << "[OK] MKDIR: carpeta creada " << it->second << endl;
}

void cmd_mkfile(map<string, string>& params) {
    if (!require_fs_session("MKFILE")) return;
    auto pit = params.find("path");
    if (pit == params.end() || pit->second.empty()) {
        cout << "[ERROR] MKFILE: falta parametro -path" << endl;
        return;
    }

    int requested_size = 0;
    auto sit = params.find("size");
    if (sit != params.end()) {
        if (!strict_stoi(sit->second, requested_size) || requested_size < 0) {
            cout << "[ERROR] MKFILE: -size debe ser un entero mayor o igual a 0" << endl;
            return;
        }
    }

    string content;
    string error;
    auto cit = params.find("cont");
    if (cit != params.end()) {
        if (!read_host_file(cit->second, content, error)) {
            cout << "[ERROR] MKFILE: " << error << endl;
            return;
        }
    } else {
        content = generated_content(requested_size);
    }

    vector<string> parts;
    if (!parse_absolute_path(pit->second, parts, error) || parts.empty()) {
        cout << "[ERROR] MKFILE: " << (error.empty() ? "ruta invalida" : error) << endl;
        return;
    }
    bool recursive = params.count("r") > 0;

    string disk_path;
    Partition part;
    Superblock sb;
    if (!fs_load_context(current_session.id, disk_path, part, sb, error)) {
        cout << "[ERROR] MKFILE: " << error << endl;
        return;
    }

    FILE* file = fopen(disk_path.c_str(), "r+b");
    if (!file) {
        cout << "[ERROR] MKFILE: no se pudo abrir el disco" << endl;
        return;
    }

    string leaf = parts.back();
    parts.pop_back();
    int parent_index = -1;
    Inode parent{};
    if (!ensure_parent_directory(file, part.part_start, sb, parts, recursive,
                                 parent_index, parent, error)) {
        fclose(file);
        cout << "[ERROR] MKFILE: " << error << endl;
        return;
    }

    bool overwritten = false;
    bool ok = create_or_overwrite_file(file, part.part_start, sb, parent_index,
                                       parent, leaf, content, error, overwritten);
    fflush(file);
    fclose(file);
    if (!ok) {
        cout << "[ERROR] MKFILE: " << error << endl;
        return;
    }

    if (overwritten) {
        cout << "[INFO] MKFILE: el archivo ya existia; se sobreescribio su contenido" << endl;
    }
    cout << "[OK] MKFILE: archivo " << pit->second << " creado (" << content.size() << " bytes)" << endl;
}

void cmd_cat(map<string, string>& params) {
    if (!require_fs_session("CAT")) return;
    if (params.empty()) {
        cout << "[ERROR] CAT: requiere al menos -file1" << endl;
        return;
    }

    vector<pair<int, string>> files;
    for (const auto& [key, value] : params) {
        if (key.rfind("file", 0) != 0 || key.size() <= 4) {
            cout << "[ERROR] CAT: parametro no reconocido -" << key << endl;
            return;
        }
        int number = 0;
        if (!strict_stoi(key.substr(4), number) || number <= 0) {
            cout << "[ERROR] CAT: los parametros deben usar -fileN con N positivo" << endl;
            return;
        }
        files.push_back({number, value});
    }
    sort(files.begin(), files.end());

    string disk_path;
    Partition part;
    Superblock sb;
    string error;
    if (!fs_load_context(current_session.id, disk_path, part, sb, error)) {
        cout << "[ERROR] CAT: " << error << endl;
        return;
    }

    FILE* file = fopen(disk_path.c_str(), "rb");
    if (!file) {
        cout << "[ERROR] CAT: no se pudo abrir el disco" << endl;
        return;
    }

    vector<string> outputs;
    for (const auto& item : files) {
        vector<string> parts;
        if (!parse_absolute_path(item.second, parts, error) || parts.empty()) {
            fclose(file);
            cout << "[ERROR] CAT: " << (error.empty() ? "ruta invalida" : error) << endl;
            return;
        }

        int inode_index = -1;
        Inode inode{};
        if (!resolve_inode_path(file, sb, parts, inode_index, inode, error)) {
            fclose(file);
            cout << "[ERROR] CAT: " << item.second << ": " << error << endl;
            return;
        }
        if (inode.i_type != '1') {
            fclose(file);
            cout << "[ERROR] CAT: " << item.second << " no es un archivo" << endl;
            return;
        }
        if (!has_permission(inode, 4)) {
            fclose(file);
            cout << "[ERROR] CAT: no tiene permiso de lectura sobre " << item.second << endl;
            return;
        }

        string content;
        if (!read_inode_file(file, sb, inode, content, error)) {
            fclose(file);
            cout << "[ERROR] CAT: " << error << endl;
            return;
        }
        outputs.push_back(content);
    }
    fclose(file);

    for (size_t i = 0; i < outputs.size(); ++i) {
        cout << outputs[i];
        if (i + 1 < outputs.size()) cout << '\n';
    }
    cout << endl;
}

void cmd_chmod(map<string, string>& params) {
    if (!require_fs_session("CHMOD")) return;
    auto pit = params.find("path");
    auto uit = params.find("ugo");
    if (pit == params.end() || uit == params.end()) {
        cout << "[ERROR] CHMOD: requiere -path y -ugo" << endl;
        return;
    }

    char perms[3];
    if (!parse_ugo(uit->second, perms)) {
        cout << "[ERROR] CHMOD: -ugo debe contener exactamente tres digitos octales (000-777)" << endl;
        return;
    }

    vector<string> parts;
    string error;
    if (!parse_absolute_path(pit->second, parts, error)) {
        cout << "[ERROR] CHMOD: " << error << endl;
        return;
    }

    string disk_path;
    Partition part;
    Superblock sb;
    if (!fs_load_context(current_session.id, disk_path, part, sb, error)) {
        cout << "[ERROR] CHMOD: " << error << endl;
        return;
    }
    FILE* file = fopen(disk_path.c_str(), "r+b");
    if (!file) {
        cout << "[ERROR] CHMOD: no se pudo abrir el disco" << endl;
        return;
    }

    int target = -1;
    Inode target_inode{};
    if (!resolve_inode_path(file, sb, parts, target, target_inode, error)) {
        fclose(file);
        cout << "[ERROR] CHMOD: " << error << endl;
        return;
    }

    bool recursive = params.count("r") > 0;
    bool root = current_session.uid == 1 && current_session.user == "root";
    auto action = [&](int index, Inode& inode, string& action_error) -> bool {
        if (!root && inode.i_uid != current_session.uid) {
            action_error = "solo root o el propietario pueden cambiar permisos";
            return false;
        }
        memcpy(inode.i_perm, perms, 3);
        inode.i_mtime = time(nullptr);
        if (!write_inode(file, sb, index, inode)) {
            action_error = "no se pudo actualizar un inodo";
            return false;
        }
        return true;
    };

    bool ok = false;
    if (recursive) ok = recurse_inode_tree(file, sb, target, action, error);
    else ok = action(target, target_inode, error);
    fflush(file);
    fclose(file);

    if (!ok) {
        cout << "[ERROR] CHMOD: " << error << endl;
        return;
    }
    cout << "[OK] CHMOD: permisos " << uit->second << " aplicados a " << pit->second
         << (recursive ? " recursivamente" : "") << endl;
}

void cmd_chown(map<string, string>& params) {
    if (!require_fs_session("CHOWN")) return;
    if (!(current_session.uid == 1 && current_session.user == "root")) {
        cout << "[ERROR] CHOWN: solo el usuario root puede cambiar propietarios" << endl;
        return;
    }

    auto pit = params.find("path");
    auto uit = params.find("user");
    if (pit == params.end() || uit == params.end()) {
        cout << "[ERROR] CHOWN: requiere -path y -user" << endl;
        return;
    }

    UserIdentity identity;
    string error;
    if (!lookup_user_identity(uit->second, identity, error)) {
        cout << "[ERROR] CHOWN: " << error << endl;
        return;
    }

    vector<string> parts;
    if (!parse_absolute_path(pit->second, parts, error)) {
        cout << "[ERROR] CHOWN: " << error << endl;
        return;
    }

    string disk_path;
    Partition part;
    Superblock sb;
    if (!fs_load_context(current_session.id, disk_path, part, sb, error)) {
        cout << "[ERROR] CHOWN: " << error << endl;
        return;
    }
    FILE* file = fopen(disk_path.c_str(), "r+b");
    if (!file) {
        cout << "[ERROR] CHOWN: no se pudo abrir el disco" << endl;
        return;
    }

    int target = -1;
    Inode target_inode{};
    if (!resolve_inode_path(file, sb, parts, target, target_inode, error)) {
        fclose(file);
        cout << "[ERROR] CHOWN: " << error << endl;
        return;
    }

    bool recursive = params.count("r") > 0;
    auto action = [&](int index, Inode& inode, string& action_error) -> bool {
        inode.i_uid = identity.uid;
        inode.i_gid = identity.gid;
        inode.i_mtime = time(nullptr);
        if (!write_inode(file, sb, index, inode)) {
            action_error = "no se pudo actualizar un inodo";
            return false;
        }
        return true;
    };

    bool ok = false;
    if (recursive) ok = recurse_inode_tree(file, sb, target, action, error);
    else ok = action(target, target_inode, error);
    fflush(file);
    fclose(file);

    if (!ok) {
        cout << "[ERROR] CHOWN: " << error << endl;
        return;
    }
    cout << "[OK] CHOWN: propietario de " << pit->second << " cambiado a " << uit->second
         << (recursive ? " recursivamente" : "") << endl;
}
