#ifndef DISK_H
#define DISK_H

#include <string>
#include <map>
#include <vector>
#include "structs.h"

using namespace std;

// lectura y escritura del MBR
MBR read_mbr(const string& path);
void write_mbr(const string& path, const MBR& mbr);

// comandos de administracion de discos
void cmd_mkdisk(map<string, string>& params);
void cmd_rmdisk(map<string, string>& params);
void cmd_fdisk(map<string, string>& params);

// Registro en memoria de discos conocidos por la instancia actual. El
// visualizador web usa esta lista para mostrar los discos creados/montados.
void register_disk_path(const string& path);
void unregister_disk_path(const string& path);
vector<string> get_known_disks();

#endif
