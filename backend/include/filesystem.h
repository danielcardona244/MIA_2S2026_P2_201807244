#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <map>
#include <string>
#include "structs.h"

using namespace std;

// Comando de formateo EXT2
void cmd_mkfs(map<string, string>& params);

// Sprint 4: administracion de archivos, carpetas y permisos.
void cmd_mkfile(map<string, string>& params);
void cmd_mkdir(map<string, string>& params);
void cmd_cat(map<string, string>& params);
void cmd_chmod(map<string, string>& params);
void cmd_chown(map<string, string>& params);

// Utilidades del sistema de archivos usadas por los comandos de usuarios.
// Todas trabajan sobre una particion previamente montada por id.
bool fs_load_context(const string& id,
                     string& out_path,
                     Partition& out_partition,
                     Superblock& out_superblock,
                     string& error);

bool fs_read_users_file(const string& id, string& content, string& error);
bool fs_write_users_file(const string& id, const string& content, string& error);

#endif

