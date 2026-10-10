#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <map>
#include <string>
#include <vector>
#include "structs.h"

using namespace std;

// Comando de formateo EXT2/EXT3
void cmd_mkfs(map<string, string>& params);
void cmd_journaling(map<string, string>& params);
void cmd_loss(map<string, string>& params);


struct JournalViewEntry {
    int count = 0;
    string operation;
    string path;
    string content;
    string date;
};

bool fs_read_journal_entries(const string& id,
                             vector<JournalViewEntry>& entries,
                             string& error);

// Datos de solo lectura usados por el explorador web del Proyecto 2.
struct FsViewEntry {
    string name;
    string path;
    string type;       // "folder" o "file"
    int inode = -1;
    int size = 0;
    string permissions;
    int uid = -1;
    int gid = -1;
    string modified;
    bool readable = false;
};

struct FsFileView {
    string name;
    string path;
    int inode = -1;
    int size = 0;
    string permissions;
    int uid = -1;
    int gid = -1;
    string modified;
    string content;
};

bool fs_list_directory_view(const string& id,
                            const string& internal_path,
                            vector<FsViewEntry>& entries,
                            string& error);

bool fs_read_file_view(const string& id,
                       const string& internal_path,
                       FsFileView& view,
                       string& error);

// Sprint 4: administracion de archivos, carpetas y permisos.
void cmd_mkfile(map<string, string>& params);
void cmd_mkdir(map<string, string>& params);
void cmd_cat(map<string, string>& params);
void cmd_chmod(map<string, string>& params);
void cmd_chown(map<string, string>& params);
void cmd_remove(map<string, string>& params);
void cmd_rename(map<string, string>& params);
void cmd_copy(map<string, string>& params);
void cmd_move(map<string, string>& params);
void cmd_find(map<string, string>& params);

// Utilidades del sistema de archivos usadas por los comandos de usuarios.
// Todas trabajan sobre una particion previamente montada por id.
bool fs_load_context(const string& id,
                     string& out_path,
                     Partition& out_partition,
                     Superblock& out_superblock,
                     string& error);

bool fs_read_users_file(const string& id, string& content, string& error);
bool fs_write_users_file(const string& id, const string& content, string& error);

// Registra una operacion si la particion es EXT3. En EXT2 es un no-op exitoso.
bool fs_append_journal(const string& id,
                       const string& operation,
                       const string& path,
                       const string& content,
                       string& error);

#endif
