#ifndef STRUCTS_H
#define STRUCTS_H

#include <ctime>

// Estructuras binarias 

typedef struct {
    char part_status = '0';
    char part_type = 'P';
    char part_fit = 'W';
    int part_start = -1;
    int part_s = 0;
    char part_name[16] = {0};
    int part_correlative = -1;
    char part_id[4] = {0};
} Partition;

typedef struct {
    int mbr_tamano = 0;
    time_t mbr_fecha_creacion = 0;
    int mbr_dsk_signature = 0;
    char dsk_fit = 'F';
    Partition mbr_partitions[4];
} MBR;

typedef struct {
    char part_mount = '0';
    char part_fit = 'W';
    int part_start = -1;
    int part_s = 0;
    int part_next = -1;
    char part_name[16] = {0};
} EBR;

typedef struct {
    int s_filesystem_type = 0;
    int s_inodes_count = 0;
    int s_blocks_count = 0;
    int s_free_blocks_count = 0;
    int s_free_inodes_count = 0;
    time_t s_mtime = 0;
    time_t s_umtime = 0;
    int s_mnt_count = 0;
    int s_magic = 0xEF53;
    int s_inode_s = 0;
    int s_block_s = 0;
    int s_firts_ino = 0;
    int s_first_blo = 0;
    int s_bm_inode_start = 0;
    int s_bm_block_start = 0;
    int s_inode_start = 0;
    int s_block_start = 0;
} Superblock;

typedef struct {
    int i_uid = 0;
    int i_gid = 0;
    int i_s = 0;
    time_t i_atime = 0;
    time_t i_ctime = 0;
    time_t i_mtime = 0;
    int i_block[15] = {
        -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1
    };
    char i_type = '0';
    char i_perm[3] = {'6', '6', '4'};
} Inode;

typedef struct {
    char b_name[12] = {0};
    int b_inodo = -1;
} Content;

typedef struct {
    Content b_content[4];
} FolderBlock;

typedef struct {
    char b_content[64] = {0};
} FileBlock;

typedef struct {
    int b_pointers[16] = {
        -1, -1, -1, -1,
        -1, -1, -1, -1,
        -1, -1, -1, -1,
        -1, -1, -1, -1
    };
} PointerBlock;

#endif
