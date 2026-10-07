#include "reports.h"

#include "disk.h"
#include "mount.h"
#include "shared.h"
#include "structs.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

namespace {

constexpr int BLOCK_SIZE = 64;
constexpr int ROOT_INODE_INDEX = 0;
constexpr int USERS_INODE_INDEX = 1;

struct ReportContext {
    string disk_path;
    string partition_name;
    Partition partition{};
    MBR mbr{};
    Superblock sb{};
    bool has_fs = false;
};

enum class BlockKind { Unknown, Folder, File, Pointer };

struct UserMaps {
    map<int, string> users;
    map<int, string> groups;
};

bool seek_file(FILE* file, long long offset) {
    if (!file || offset < 0 || offset > LONG_MAX) return false;
    return fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
}

bool read_exact(FILE* file, long long offset, void* out, size_t size) {
    return seek_file(file, offset) && fread(out, 1, size, file) == size;
}

bool read_inode(FILE* file, const Superblock& sb, int index, Inode& inode) {
    if (index < 0 || index >= sb.s_inodes_count) return false;
    long long pos = static_cast<long long>(sb.s_inode_start) +
                    static_cast<long long>(index) * sizeof(Inode);
    return read_exact(file, pos, &inode, sizeof(Inode));
}

bool read_folder_block(FILE* file, const Superblock& sb, int index, FolderBlock& block) {
    if (index < 0 || index >= sb.s_blocks_count) return false;
    long long pos = static_cast<long long>(sb.s_block_start) +
                    static_cast<long long>(index) * BLOCK_SIZE;
    return read_exact(file, pos, &block, sizeof(FolderBlock));
}

bool read_file_block(FILE* file, const Superblock& sb, int index, FileBlock& block) {
    if (index < 0 || index >= sb.s_blocks_count) return false;
    long long pos = static_cast<long long>(sb.s_block_start) +
                    static_cast<long long>(index) * BLOCK_SIZE;
    return read_exact(file, pos, &block, sizeof(FileBlock));
}

bool read_pointer_block(FILE* file, const Superblock& sb, int index, PointerBlock& block) {
    if (index < 0 || index >= sb.s_blocks_count) return false;
    long long pos = static_cast<long long>(sb.s_block_start) +
                    static_cast<long long>(index) * BLOCK_SIZE;
    return read_exact(file, pos, &block, sizeof(PointerBlock));
}

bool bitmap_used(FILE* file, int start, int count, int index) {
    if (index < 0 || index >= count) return false;
    char bit = 0;
    if (!read_exact(file, static_cast<long long>(start) + index, &bit, 1)) return false;
    return bit == '1' || bit == 1;
}

string html_escape(const string& value) {
    string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            case '\n': out += "<BR ALIGN=\"LEFT\"/>"; break;
            case '\r': break;
            case '\t': out += "    "; break;
            default:
                if (static_cast<unsigned char>(c) < 32) out += '.';
                else out += c;
        }
    }
    return out;
}

string dot_quote(const string& value) {
    string out;
    for (char c : value) {
        if (c == '\\' || c == '"') out += '\\';
        if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    return out;
}

string shell_quote(const string& value) {
    string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

string format_time(time_t t, const char* pattern = "%Y-%m-%d %H:%M:%S") {
    if (t == 0) return "0";
    tm info{};
#if defined(_WIN32)
    localtime_s(&info, &t);
#else
    localtime_r(&t, &info);
#endif
    char buffer[64] = {0};
    if (strftime(buffer, sizeof(buffer), pattern, &info) == 0) return to_string(static_cast<long long>(t));
    return buffer;
}

string lower_ext(const string& path) {
    string ext = fs::path(path).extension().string();
    return to_lower(ext);
}

bool ensure_parent(const string& output, string& error) {
    try {
        fs::path parent = fs::path(output).parent_path();
        if (!parent.empty()) fs::create_directories(parent);
        return true;
    } catch (const fs::filesystem_error& e) {
        error = string("no se pudo crear la carpeta del reporte: ") + e.what();
        return false;
    }
}

bool write_text(const string& output, const string& content, string& error) {
    if (!ensure_parent(output, error)) return false;
    ofstream out(output, ios::binary | ios::trunc);
    if (!out) {
        error = "no se pudo crear el archivo de reporte";
        return false;
    }
    out << content;
    if (!out) {
        error = "no se pudo escribir el reporte";
        return false;
    }
    return true;
}

bool render_dot(const string& output, const string& dot, string& error) {
    if (!ensure_parent(output, error)) return false;

    string ext = lower_ext(output);
    string format;
    if (ext == ".jpg" || ext == ".jpeg") format = "jpg";
    else if (ext == ".png") format = "png";
    else if (ext == ".pdf") format = "pdf";
    else if (ext == ".svg") format = "svg";
    else if (ext == ".dot") return write_text(output, dot, error);
    else {
        error = "extension no soportada para reporte Graphviz (usar jpg, jpeg, png, pdf, svg o dot)";
        return false;
    }

    fs::path temp = fs::path(output);
    temp += ".tmp.dot";
    if (!write_text(temp.string(), dot, error)) return false;

    string command = "dot -T" + format + " " + shell_quote(temp.string()) + " -o " + shell_quote(output);
    int rc = system(command.c_str());
    std::error_code ec;
    fs::remove(temp, ec);
    if (rc != 0 || !fs::exists(output) || fs::file_size(output, ec) == 0) {
        error = "Graphviz no pudo generar el reporte; verifique que 'dot' este instalado";
        return false;
    }
    return true;
}

bool find_primary_partition(const MBR& mbr, const string& name, Partition& out) {
    for (const Partition& p : mbr.mbr_partitions) {
        if (p.part_s > 0 && p.part_start >= 0 && p.part_type == 'P' &&
            compare_ci(read_fixed(p.part_name, 16), name)) {
            out = p;
            return true;
        }
    }
    return false;
}

bool load_context(const string& id, bool require_fs, ReportContext& ctx, string& error) {
    if (!get_mount_info(id, ctx.disk_path, ctx.partition_name)) {
        error = "el id no corresponde a una particion montada";
        return false;
    }

    FILE* file = fopen(ctx.disk_path.c_str(), "rb");
    if (!file) {
        error = "no se pudo abrir el disco";
        return false;
    }
    bool ok = fread(&ctx.mbr, sizeof(MBR), 1, file) == 1;
    if (!ok) {
        fclose(file);
        error = "no se pudo leer el MBR";
        return false;
    }
    if (!find_primary_partition(ctx.mbr, ctx.partition_name, ctx.partition)) {
        fclose(file);
        error = "la particion montada ya no existe o no es primaria";
        return false;
    }

    if (require_fs) {
        if (!read_exact(file, ctx.partition.part_start, &ctx.sb, sizeof(Superblock))) {
            fclose(file);
            error = "no se pudo leer el superbloque";
            return false;
        }
        if (ctx.sb.s_magic != 0xEF53 || ctx.sb.s_filesystem_type != 2) {
            fclose(file);
            error = "la particion no tiene un sistema de archivos EXT2 valido";
            return false;
        }
        ctx.has_fs = true;
    }
    fclose(file);
    return true;
}

vector<pair<int, EBR>> read_ebrs(FILE* file, const Partition& ext) {
    vector<pair<int, EBR>> result;
    if (ext.part_type != 'E' || ext.part_start < 0 || ext.part_s <= 0) return result;
    int end = ext.part_start + ext.part_s;
    int pos = ext.part_start;
    set<int> visited;
    while (pos >= ext.part_start && pos < end && !visited.count(pos) && result.size() < 1024) {
        visited.insert(pos);
        EBR ebr{};
        if (!read_exact(file, pos, &ebr, sizeof(EBR))) break;
        if (ebr.part_s > 0) result.push_back({pos, ebr});
        if (ebr.part_next == -1) break;
        pos = ebr.part_next;
    }
    return result;
}

string table_header(const string& title, int columns = 2) {
    ostringstream out;
    out << "<TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"5\">"
        << "<TR><TD COLSPAN=\"" << columns << "\" BGCOLOR=\"#17365D\"><FONT COLOR=\"white\"><B>"
        << html_escape(title) << "</B></FONT></TD></TR>";
    return out.str();
}

void row(ostringstream& out, const string& key, const string& value) {
    out << "<TR><TD ALIGN=\"LEFT\"><B>" << html_escape(key) << "</B></TD><TD ALIGN=\"LEFT\">"
        << html_escape(value) << "</TD></TR>";
}

string graph_start(const string& rankdir = "TB") {
    return "digraph G {\nrankdir=" + rankdir + ";\ngraph [dpi=160, bgcolor=\"white\"];\n"
           "node [shape=plaintext, fontname=\"Arial\"];\nedge [fontname=\"Arial\"];\n";
}

bool report_mbr(const ReportContext& ctx, const string& output, string& error) {
    FILE* file = fopen(ctx.disk_path.c_str(), "rb");
    if (!file) { error = "no se pudo abrir el disco"; return false; }

    ostringstream dot;
    dot << graph_start();
    dot << "mbr [label=<" << table_header("REPORTE MBR") ;
    row(dot, "mbr_tamano", to_string(ctx.mbr.mbr_tamano));
    row(dot, "mbr_fecha_creacion", format_time(ctx.mbr.mbr_fecha_creacion));
    row(dot, "mbr_dsk_signature", to_string(ctx.mbr.mbr_dsk_signature));
    row(dot, "dsk_fit", string(1, ctx.mbr.dsk_fit));

    for (int i = 0; i < 4; ++i) {
        const Partition& p = ctx.mbr.mbr_partitions[i];
        dot << "<TR><TD COLSPAN=\"2\" BGCOLOR=\"#D9EAF7\"><B>PARTICION " << (i + 1) << "</B></TD></TR>";
        row(dot, "part_status", string(1, p.part_status));
        row(dot, "part_type", string(1, p.part_type));
        row(dot, "part_fit", string(1, p.part_fit));
        row(dot, "part_start", to_string(p.part_start));
        row(dot, "part_s", to_string(p.part_s));
        row(dot, "part_name", read_fixed(p.part_name, 16));
        row(dot, "part_correlative", to_string(p.part_correlative));
        row(dot, "part_id", read_fixed(p.part_id, 4));

        if (p.part_s > 0 && p.part_type == 'E') {
            auto ebrs = read_ebrs(file, p);
            for (size_t e = 0; e < ebrs.size(); ++e) {
                const EBR& eb = ebrs[e].second;
                dot << "<TR><TD COLSPAN=\"2\" BGCOLOR=\"#FCE4D6\"><B>EBR " << (e + 1) << "</B></TD></TR>";
                row(dot, "part_mount", string(1, eb.part_mount));
                row(dot, "part_fit", string(1, eb.part_fit));
                row(dot, "part_start", to_string(eb.part_start));
                row(dot, "part_s", to_string(eb.part_s));
                row(dot, "part_next", to_string(eb.part_next));
                row(dot, "part_name", read_fixed(eb.part_name, 16));
            }
        }
    }
    fclose(file);
    dot << "</TABLE>>];\n}\n";
    return render_dot(output, dot.str(), error);
}

struct DiskSegment { int start; int size; string label; bool extended = false; Partition part{}; };

bool report_disk(const ReportContext& ctx, const string& output, string& error) {
    vector<DiskSegment> used;
    for (const Partition& p : ctx.mbr.mbr_partitions) {
        if (p.part_s > 0 && p.part_start >= static_cast<int>(sizeof(MBR))) {
            used.push_back({p.part_start, p.part_s,
                            p.part_type == 'E' ? "EXTENDIDA " + read_fixed(p.part_name,16)
                                               : "PRIMARIA " + read_fixed(p.part_name,16),
                            p.part_type == 'E', p});
        }
    }
    sort(used.begin(), used.end(), [](const auto& a, const auto& b){ return a.start < b.start; });

    vector<DiskSegment> segments;
    segments.push_back({0, static_cast<int>(sizeof(MBR)), "MBR", false, {}});
    int cursor = static_cast<int>(sizeof(MBR));
    for (const auto& s : used) {
        if (s.start > cursor) segments.push_back({cursor, s.start - cursor, "LIBRE", false, {}});
        segments.push_back(s);
        cursor = max(cursor, s.start + s.size);
    }
    if (cursor < ctx.mbr.mbr_tamano) segments.push_back({cursor, ctx.mbr.mbr_tamano - cursor, "LIBRE", false, {}});

    FILE* file = fopen(ctx.disk_path.c_str(), "rb");
    if (!file) { error = "no se pudo abrir el disco"; return false; }

    ostringstream dot;
    dot << graph_start("LR");
    dot << "disk [label=<<TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"8\"><TR>";
    double percent_sum = 0.0;
    for (const auto& s : segments) {
        double pct = ctx.mbr.mbr_tamano > 0 ? (100.0 * s.size / ctx.mbr.mbr_tamano) : 0.0;
        percent_sum += pct;
        dot << "<TD>" << html_escape(s.label) << "<BR/><B>" << fixed << setprecision(2) << pct << "%</B>";
        if (s.extended) {
            auto ebrs = read_ebrs(file, s.part);
            if (!ebrs.empty()) {
                dot << "<BR/><FONT POINT-SIZE=\"9\">";
                for (size_t i = 0; i < ebrs.size(); ++i) {
                    if (i) dot << " | ";
                    double lpct = 100.0 * (sizeof(EBR) + ebrs[i].second.part_s) / ctx.mbr.mbr_tamano;
                    dot << "EBR+LOGICA " << html_escape(read_fixed(ebrs[i].second.part_name,16))
                        << " " << fixed << setprecision(2) << lpct << "%";
                }
                dot << "</FONT>";
            }
        }
        dot << "</TD>";
    }
    fclose(file);
    dot << "</TR><TR><TD COLSPAN=\"" << max<size_t>(1, segments.size()) << "\">Total representado: "
        << fixed << setprecision(2) << percent_sum << "%</TD></TR></TABLE>>];\n}\n";
    return render_dot(output, dot.str(), error);
}

bool collect_pointer_tree(FILE* file, const Superblock& sb, int index, int level,
                          BlockKind data_kind, map<int, BlockKind>& kinds,
                          vector<pair<int,int>>& pointer_edges, string& error) {
    if (index < 0 || index >= sb.s_blocks_count) { error = "apuntador fuera de rango"; return false; }
    kinds[index] = BlockKind::Pointer;
    PointerBlock pb{};
    if (!read_pointer_block(file, sb, index, pb)) { error = "no se pudo leer un bloque apuntador"; return false; }
    for (int child : pb.b_pointers) {
        if (child == -1) continue;
        if (child < 0 || child >= sb.s_blocks_count) { error = "apuntador hijo fuera de rango"; return false; }
        pointer_edges.push_back({index, child});
        if (level == 1) kinds[child] = data_kind;
        else if (!collect_pointer_tree(file, sb, child, level - 1, data_kind, kinds, pointer_edges, error)) return false;
    }
    return true;
}

bool classify_blocks(FILE* file, const Superblock& sb,
                     map<int, BlockKind>& kinds,
                     vector<pair<int,int>>& inode_edges,
                     vector<pair<int,int>>& pointer_edges,
                     string& error) {
    kinds.clear(); inode_edges.clear(); pointer_edges.clear();
    for (int i = 0; i < sb.s_inodes_count; ++i) {
        if (!bitmap_used(file, sb.s_bm_inode_start, sb.s_inodes_count, i)) continue;
        Inode inode{};
        if (!read_inode(file, sb, i, inode)) { error = "no se pudo leer un inodo"; return false; }
        BlockKind data_kind = inode.i_type == '0' ? BlockKind::Folder : BlockKind::File;
        for (int j = 0; j < 12; ++j) {
            if (inode.i_block[j] == -1) continue;
            kinds[inode.i_block[j]] = data_kind;
            inode_edges.push_back({i, inode.i_block[j]});
        }
        for (int j = 12; j < 15; ++j) {
            if (inode.i_block[j] == -1) continue;
            inode_edges.push_back({i, inode.i_block[j]});
            if (!collect_pointer_tree(file, sb, inode.i_block[j], j - 11, data_kind, kinds, pointer_edges, error)) return false;
        }
    }
    for (int b = 0; b < sb.s_blocks_count; ++b) {
        if (bitmap_used(file, sb.s_bm_block_start, sb.s_blocks_count, b) && !kinds.count(b)) kinds[b] = BlockKind::Unknown;
    }
    return true;
}

string inode_table(int index, const Inode& inode) {
    ostringstream out;
    out << table_header("INODO " + to_string(index));
    row(out, "i_uid", to_string(inode.i_uid));
    row(out, "i_gid", to_string(inode.i_gid));
    row(out, "i_s", to_string(inode.i_s));
    row(out, "i_atime", format_time(inode.i_atime));
    row(out, "i_ctime", format_time(inode.i_ctime));
    row(out, "i_mtime", format_time(inode.i_mtime));
    for (int j = 0; j < 15; ++j) row(out, "i_block[" + to_string(j) + "]", to_string(inode.i_block[j]));
    row(out, "i_type", string(1, inode.i_type));
    row(out, "i_perm", string(inode.i_perm, inode.i_perm + 3));
    out << "</TABLE>";
    return out.str();
}

bool report_inode(const ReportContext& ctx, const string& output, string& error) {
    FILE* file = fopen(ctx.disk_path.c_str(), "rb");
    if (!file) { error = "no se pudo abrir el disco"; return false; }
    ostringstream dot; dot << graph_start("LR");
    int previous = -1;
    for (int i = 0; i < ctx.sb.s_inodes_count; ++i) {
        if (!bitmap_used(file, ctx.sb.s_bm_inode_start, ctx.sb.s_inodes_count, i)) continue;
        Inode inode{};
        if (!read_inode(file, ctx.sb, i, inode)) { fclose(file); error = "no se pudo leer un inodo"; return false; }
        dot << "inode" << i << " [label=<" << inode_table(i, inode) << ">];\n";
        if (previous >= 0) dot << "inode" << previous << " -> inode" << i << " [style=dashed, color=gray];\n";
        previous = i;
    }
    fclose(file); dot << "}\n";
    return render_dot(output, dot.str(), error);
}

string block_table(FILE* file, const Superblock& sb, int index, BlockKind kind, string& error) {
    ostringstream out;
    string title = "BLOQUE " + to_string(index) + " - ";
    if (kind == BlockKind::Folder) title += "CARPETA";
    else if (kind == BlockKind::File) title += "ARCHIVO";
    else if (kind == BlockKind::Pointer) title += "APUNTADORES";
    else title += "USADO";
    out << table_header(title);

    if (kind == BlockKind::Folder) {
        FolderBlock fb{};
        if (!read_folder_block(file, sb, index, fb)) { error = "no se pudo leer bloque de carpeta"; return ""; }
        for (int i = 0; i < 4; ++i) {
            string value = read_fixed(fb.b_content[i].b_name, 12) + " -> " + to_string(fb.b_content[i].b_inodo);
            row(out, "b_content[" + to_string(i) + "]", value);
        }
    } else if (kind == BlockKind::File) {
        FileBlock fb{};
        if (!read_file_block(file, sb, index, fb)) { error = "no se pudo leer bloque de archivo"; return ""; }
        size_t len = 0;
        while (len < sizeof(fb.b_content) && fb.b_content[len] != '\0') ++len;
        row(out, "b_content", string(fb.b_content, fb.b_content + len));
    } else if (kind == BlockKind::Pointer) {
        PointerBlock pb{};
        if (!read_pointer_block(file, sb, index, pb)) { error = "no se pudo leer bloque apuntador"; return ""; }
        for (int i = 0; i < 16; ++i) row(out, "b_pointers[" + to_string(i) + "]", to_string(pb.b_pointers[i]));
    } else {
        row(out, "estado", "bloque marcado como utilizado");
    }
    out << "</TABLE>";
    return out.str();
}

bool report_block(const ReportContext& ctx, const string& output, string& error) {
    FILE* file = fopen(ctx.disk_path.c_str(), "rb");
    if (!file) { error = "no se pudo abrir el disco"; return false; }
    map<int, BlockKind> kinds; vector<pair<int,int>> ie, pe;
    if (!classify_blocks(file, ctx.sb, kinds, ie, pe, error)) { fclose(file); return false; }
    ostringstream dot; dot << graph_start("LR");
    int previous = -1;
    for (auto& [index, kind] : kinds) {
        if (!bitmap_used(file, ctx.sb.s_bm_block_start, ctx.sb.s_blocks_count, index)) continue;
        string table = block_table(file, ctx.sb, index, kind, error);
        if (table.empty() && !error.empty()) { fclose(file); return false; }
        dot << "block" << index << " [label=<" << table << ">];\n";
        if (previous >= 0) dot << "block" << previous << " -> block" << index << " [style=dashed, color=gray];\n";
        previous = index;
    }
    fclose(file); dot << "}\n";
    return render_dot(output, dot.str(), error);
}

bool report_bitmap(const ReportContext& ctx, const string& output, bool inode_bitmap, string& error) {
    FILE* file = fopen(ctx.disk_path.c_str(), "rb");
    if (!file) { error = "no se pudo abrir el disco"; return false; }
    int start = inode_bitmap ? ctx.sb.s_bm_inode_start : ctx.sb.s_bm_block_start;
    int count = inode_bitmap ? ctx.sb.s_inodes_count : ctx.sb.s_blocks_count;
    ostringstream out;
    for (int i = 0; i < count; ++i) {
        char bit = 0;
        if (!read_exact(file, static_cast<long long>(start) + i, &bit, 1)) { fclose(file); error = "no se pudo leer el bitmap"; return false; }
        char normalized = (bit == '1' || bit == 1) ? '1' : '0';
        out << normalized;
        if ((i + 1) % 20 == 0 || i + 1 == count) out << '\n';
        else out << ' ';
    }
    fclose(file);
    return write_text(output, out.str(), error);
}

bool report_sb(const ReportContext& ctx, const string& output, string& error) {
    ostringstream dot; dot << graph_start();
    dot << "sb [label=<" << table_header("REPORTE SUPERBLOQUE");
    row(dot, "s_filesystem_type", to_string(ctx.sb.s_filesystem_type));
    row(dot, "s_inodes_count", to_string(ctx.sb.s_inodes_count));
    row(dot, "s_blocks_count", to_string(ctx.sb.s_blocks_count));
    row(dot, "s_free_blocks_count", to_string(ctx.sb.s_free_blocks_count));
    row(dot, "s_free_inodes_count", to_string(ctx.sb.s_free_inodes_count));
    row(dot, "s_mtime", format_time(ctx.sb.s_mtime));
    row(dot, "s_umtime", format_time(ctx.sb.s_umtime));
    row(dot, "s_mnt_count", to_string(ctx.sb.s_mnt_count));
    { ostringstream h; h << "0x" << hex << uppercase << ctx.sb.s_magic; row(dot, "s_magic", h.str()); }
    row(dot, "s_inode_s", to_string(ctx.sb.s_inode_s));
    row(dot, "s_block_s", to_string(ctx.sb.s_block_s));
    row(dot, "s_firts_ino", to_string(ctx.sb.s_firts_ino));
    row(dot, "s_first_blo", to_string(ctx.sb.s_first_blo));
    row(dot, "s_bm_inode_start", to_string(ctx.sb.s_bm_inode_start));
    row(dot, "s_bm_block_start", to_string(ctx.sb.s_bm_block_start));
    row(dot, "s_inode_start", to_string(ctx.sb.s_inode_start));
    row(dot, "s_block_start", to_string(ctx.sb.s_block_start));
    dot << "</TABLE>>];\n}\n";
    return render_dot(output, dot.str(), error);
}

bool parse_path(const string& raw, vector<string>& parts, string& error) {
    parts.clear();
    if (raw.empty() || raw[0] != '/') { error = "path_file_ls debe ser una ruta absoluta"; return false; }
    string current;
    for (size_t i = 1; i <= raw.size(); ++i) {
        char c = i < raw.size() ? raw[i] : '/';
        if (c == '/') {
            if (!current.empty()) { parts.push_back(current); current.clear(); }
        } else current += c;
    }
    return true;
}

bool collect_data_from_pointer(FILE* file, const Superblock& sb, int pointer, int level,
                               vector<int>& data, string& error) {
    PointerBlock pb{};
    if (!read_pointer_block(file, sb, pointer, pb)) { error = "no se pudo leer apuntador"; return false; }
    for (int child : pb.b_pointers) {
        if (child == -1) continue;
        if (level == 1) data.push_back(child);
        else if (!collect_data_from_pointer(file, sb, child, level - 1, data, error)) return false;
    }
    return true;
}

bool inode_data_blocks(FILE* file, const Superblock& sb, const Inode& inode, vector<int>& data, string& error) {
    data.clear();
    for (int i = 0; i < 12; ++i) if (inode.i_block[i] != -1) data.push_back(inode.i_block[i]);
    for (int i = 12; i < 15; ++i) if (inode.i_block[i] != -1 &&
        !collect_data_from_pointer(file, sb, inode.i_block[i], i - 11, data, error)) return false;
    return true;
}

bool find_child(FILE* file, const Superblock& sb, const Inode& dir, const string& name, int& out, string& error) {
    if (dir.i_type != '0') { error = "un componente de la ruta no es carpeta"; return false; }
    vector<int> blocks;
    if (!inode_data_blocks(file, sb, dir, blocks, error)) return false;
    for (int b : blocks) {
        FolderBlock fb{};
        if (!read_folder_block(file, sb, b, fb)) { error = "no se pudo leer carpeta"; return false; }
        for (const Content& e : fb.b_content) {
            if (e.b_inodo >= 0 && read_fixed(e.b_name,12) == name) { out = e.b_inodo; return true; }
        }
    }
    out = -1; return true;
}

bool resolve_path(FILE* file, const Superblock& sb, const string& raw,
                  int& inode_index, Inode& inode, string& error) {
    vector<string> parts;
    if (!parse_path(raw, parts, error)) return false;
    inode_index = ROOT_INODE_INDEX;
    if (!read_inode(file, sb, inode_index, inode)) { error = "no se pudo leer la raiz"; return false; }
    for (const string& part : parts) {
        int child = -1;
        if (!find_child(file, sb, inode, part, child, error)) return false;
        if (child < 0) { error = "no existe path_file_ls"; return false; }
        if (!read_inode(file, sb, child, inode)) { error = "no se pudo leer inodo de la ruta"; return false; }
        inode_index = child;
    }
    return true;
}

bool read_inode_content(FILE* file, const Superblock& sb, const Inode& inode, string& content, string& error) {
    if (inode.i_type != '1') { error = "la ruta no corresponde a un archivo"; return false; }
    vector<int> blocks;
    if (!inode_data_blocks(file, sb, inode, blocks, error)) return false;
    content.clear(); int remaining = inode.i_s;
    for (int b : blocks) {
        if (remaining <= 0) break;
        FileBlock fb{};
        if (!read_file_block(file, sb, b, fb)) { error = "no se pudo leer bloque de archivo"; return false; }
        int take = min(BLOCK_SIZE, remaining);
        content.append(fb.b_content, fb.b_content + take);
        remaining -= take;
    }
    if (remaining != 0) { error = "el archivo no contiene suficientes bloques"; return false; }
    return true;
}

bool load_user_maps(FILE* file, const Superblock& sb, UserMaps& maps, string& error) {
    Inode users{};
    if (!read_inode(file, sb, USERS_INODE_INDEX, users)) { error = "no se pudo leer users.txt"; return false; }
    string content;
    if (!read_inode_content(file, sb, users, content, error)) return false;
    string line;
    stringstream ss(content);
    while (getline(ss, line)) {
        vector<string> fields; string f; stringstream ls(line);
        while (getline(ls, f, ',')) fields.push_back(trim(f));
        if (fields.size() < 3) continue;
        int id = -1; if (!strict_stoi(fields[0], id) || id <= 0) continue;
        if (fields[1] == "G") maps.groups[id] = fields[2];
        else if (fields[1] == "U" && fields.size() >= 5) maps.users[id] = fields[3];
    }
    return true;
}

string symbolic_permissions(const Inode& inode) {
    string out;
    for (int p = 0; p < 3; ++p) {
        int d = (inode.i_perm[p] >= '0' && inode.i_perm[p] <= '7') ? inode.i_perm[p] - '0' : 0;
        out += (d & 4) ? 'r' : '-';
        out += (d & 2) ? 'w' : '-';
        out += (d & 1) ? 'x' : '-';
    }
    return out;
}

bool report_file(const ReportContext& ctx, const string& output, const string& target, string& error) {
    FILE* file = fopen(ctx.disk_path.c_str(), "rb");
    if (!file) { error = "no se pudo abrir el disco"; return false; }
    int index; Inode inode{};
    if (!resolve_path(file, ctx.sb, target, index, inode, error)) { fclose(file); return false; }
    string content;
    if (!read_inode_content(file, ctx.sb, inode, content, error)) { fclose(file); return false; }
    fclose(file);

    string ext = lower_ext(output);
    if (ext == ".txt") {
        return write_text(output, "Archivo: " + target + "\n" + content + (content.empty() || content.back()=='\n' ? "" : "\n"), error);
    }
    ostringstream dot; dot << graph_start();
    dot << "file [label=<<TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"6\">"
        << "<TR><TD BGCOLOR=\"#17365D\"><FONT COLOR=\"white\"><B>" << html_escape(target) << "</B></FONT></TD></TR>"
        << "<TR><TD ALIGN=\"LEFT\">" << html_escape(content) << "</TD></TR></TABLE>>];\n}\n";
    return render_dot(output, dot.str(), error);
}

bool list_entries(FILE* file, const Superblock& sb, int index, const Inode& inode,
                  vector<pair<string,int>>& entries, string& error) {
    entries.clear();
    if (inode.i_type == '1') { entries.push_back({"", index}); return true; }
    vector<int> blocks;
    if (!inode_data_blocks(file, sb, inode, blocks, error)) return false;
    for (int b : blocks) {
        FolderBlock fb{};
        if (!read_folder_block(file, sb, b, fb)) { error = "no se pudo leer carpeta"; return false; }
        for (const Content& e : fb.b_content) {
            if (e.b_inodo < 0) continue;
            string name = read_fixed(e.b_name,12);
            if (name == "." || name == ".." || name.empty()) continue;
            entries.push_back({name, e.b_inodo});
        }
    }
    return true;
}

bool report_ls(const ReportContext& ctx, const string& output, const string& target, string& error) {
    FILE* file = fopen(ctx.disk_path.c_str(), "rb");
    if (!file) { error = "no se pudo abrir el disco"; return false; }
    int index; Inode inode{};
    if (!resolve_path(file, ctx.sb, target, index, inode, error)) { fclose(file); return false; }
    vector<pair<string,int>> entries;
    if (!list_entries(file, ctx.sb, index, inode, entries, error)) { fclose(file); return false; }
    UserMaps maps;
    if (!load_user_maps(file, ctx.sb, maps, error)) { fclose(file); return false; }

    ostringstream dot; dot << graph_start();
    dot << "ls [label=<<TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"5\">"
        << "<TR><TD COLSPAN=\"9\" BGCOLOR=\"#17365D\"><FONT COLOR=\"white\"><B>REPORTE LS " << html_escape(target) << "</B></FONT></TD></TR>"
        << "<TR><TD><B>Permisos</B></TD><TD><B>Owner</B></TD><TD><B>Grupo</B></TD><TD><B>Size</B></TD>"
        << "<TD><B>Modificacion</B></TD><TD><B>Creacion</B></TD><TD><B>Tipo</B></TD><TD><B>Inodo</B></TD><TD><B>Name</B></TD></TR>";

    for (auto& [name, child_index] : entries) {
        Inode child{};
        if (!read_inode(file, ctx.sb, child_index, child)) { fclose(file); error = "no se pudo leer un inodo para LS"; return false; }
        string display_name = name;
        if (display_name.empty()) display_name = fs::path(target).filename().string();
        if (display_name.empty()) display_name = "/";
        string owner = maps.users.count(child.i_uid) ? maps.users[child.i_uid] : to_string(child.i_uid);
        string group = maps.groups.count(child.i_gid) ? maps.groups[child.i_gid] : to_string(child.i_gid);
        dot << "<TR><TD>" << html_escape(symbolic_permissions(child) + " (" + string(child.i_perm,child.i_perm+3) + ")") << "</TD>"
            << "<TD>" << html_escape(owner) << "</TD><TD>" << html_escape(group) << "</TD>"
            << "<TD>" << child.i_s << "</TD><TD>" << html_escape(format_time(child.i_mtime)) << "</TD>"
            << "<TD>" << html_escape(format_time(child.i_ctime)) << "</TD>"
            << "<TD>" << (child.i_type == '0' ? "Carpeta" : "Archivo") << "</TD><TD>" << child_index << "</TD>"
            << "<TD>" << html_escape(display_name) << "</TD></TR>";
    }
    fclose(file);
    dot << "</TABLE>>];\n}\n";
    return render_dot(output, dot.str(), error);
}

bool report_tree(const ReportContext& ctx, const string& output, string& error) {
    FILE* file = fopen(ctx.disk_path.c_str(), "rb");
    if (!file) { error = "no se pudo abrir el disco"; return false; }
    map<int, BlockKind> kinds; vector<pair<int,int>> inode_edges, pointer_edges;
    if (!classify_blocks(file, ctx.sb, kinds, inode_edges, pointer_edges, error)) { fclose(file); return false; }

    ostringstream dot; dot << graph_start("LR");
    for (int i = 0; i < ctx.sb.s_inodes_count; ++i) {
        if (!bitmap_used(file, ctx.sb.s_bm_inode_start, ctx.sb.s_inodes_count, i)) continue;
        Inode inode{};
        if (!read_inode(file, ctx.sb, i, inode)) { fclose(file); error = "no se pudo leer inodo"; return false; }
        dot << "inode" << i << " [label=<" << inode_table(i, inode) << ">];\n";
    }
    for (auto& [index, kind] : kinds) {
        if (!bitmap_used(file, ctx.sb.s_bm_block_start, ctx.sb.s_blocks_count, index)) continue;
        string table = block_table(file, ctx.sb, index, kind, error);
        if (table.empty() && !error.empty()) { fclose(file); return false; }
        dot << "block" << index << " [label=<" << table << ">];\n";
    }
    for (auto& e : inode_edges) dot << "inode" << e.first << " -> block" << e.second << ";\n";
    for (auto& e : pointer_edges) dot << "block" << e.first << " -> block" << e.second << ";\n";

    // Los bloques carpeta tambien apuntan hacia los inodos de sus entradas.
    for (auto& [index, kind] : kinds) {
        if (kind != BlockKind::Folder) continue;
        FolderBlock fb{};
        if (!read_folder_block(file, ctx.sb, index, fb)) continue;
        for (const Content& c : fb.b_content) {
            if (c.b_inodo < 0) continue;
            string name = read_fixed(c.b_name,12);
            if (name == "." || name == "..") continue;
            dot << "block" << index << " -> inode" << c.b_inodo << " [label=\"" << dot_quote(name) << "\"];\n";
        }
    }
    fclose(file); dot << "}\n";
    return render_dot(output, dot.str(), error);
}

} // namespace

void cmd_rep(map<string, string>& params) {
    for (const string& key : {"name", "path", "id"}) {
        auto it = params.find(key);
        if (it == params.end() || it->second.empty()) {
            cout << "[ERROR] REP: falta parametro -" << key << endl;
            return;
        }
    }

    string name = to_lower(params["name"]);
    if (name == "bm_bloc") name = "bm_block"; // tolerar el typo que aparece en el enunciado.
    const set<string> valid = {"mbr","disk","inode","block","bm_inode","bm_block","tree","sb","file","ls"};
    if (!valid.count(name)) {
        cout << "[ERROR] REP: nombre de reporte no valido" << endl;
        return;
    }

    string output = params["path"];
    if (output.empty()) {
        cout << "[ERROR] REP: -path no puede estar vacio" << endl;
        return;
    }

    string target;
    auto path_it = params.find("path_file_ls");
    if (path_it != params.end()) target = path_it->second;
    if ((name == "file" || name == "ls") && target.empty()) {
        cout << "[ERROR] REP: -path_file_ls es obligatorio para el reporte " << name << endl;
        return;
    }

    bool require_fs = !(name == "mbr" || name == "disk");
    ReportContext ctx;
    string error;
    if (!load_context(params["id"], require_fs, ctx, error)) {
        cout << "[ERROR] REP: " << error << endl;
        return;
    }

    bool ok = false;
    if (name == "mbr") ok = report_mbr(ctx, output, error);
    else if (name == "disk") ok = report_disk(ctx, output, error);
    else if (name == "inode") ok = report_inode(ctx, output, error);
    else if (name == "block") ok = report_block(ctx, output, error);
    else if (name == "bm_inode") ok = report_bitmap(ctx, output, true, error);
    else if (name == "bm_block") ok = report_bitmap(ctx, output, false, error);
    else if (name == "tree") ok = report_tree(ctx, output, error);
    else if (name == "sb") ok = report_sb(ctx, output, error);
    else if (name == "file") ok = report_file(ctx, output, target, error);
    else if (name == "ls") ok = report_ls(ctx, output, target, error);

    if (!ok) {
        cout << "[ERROR] REP: " << error << endl;
        return;
    }
    cout << "[OK] REP: reporte " << name << " generado en " << output << endl;
}
