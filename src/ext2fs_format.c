#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <sys/types.h>
#include "../external/ext2fs/ext2fs.h"
#include "../external/ext2fs/ext2_io.h"

#define KB 1024ULL
#define MB (1024ULL * KB)
#define GB (1024ULL * MB)
#define TB (1024ULL * GB)
#define IS_POWER_OF_2(x) (((x) != 0) && (((x) & ((x) - 1)) == 0))

extern io_manager nt_io_manager;

typedef struct {
	uint64_t max_size;
	uint32_t block_size;
	uint32_t inode_size;
	uint32_t inode_ratio;
} ext2fs_default_t;

bool FormatDriveExt4Native(const char* volume_name, char* errorMsg, size_t errorMsgSize, void (*progress_cb)(int, const char*)) {
    const float reserve_ratio = 0.05f;
	const ext2fs_default_t ext2fs_default[5] = {
		{ 3 * MB, 1024, 128, 3},	// "floppy"
		{ 512 * MB, 1024, 128, 2},	// "small"
		{ 4 * GB, 4096, 256, 2},	// "default"
		{ 16 * GB, 4096, 256, 3},	// "big"
		{ 1024 * TB, 4096, 256, 4}	// "huge"
	};

	struct ext2_super_block features = { 0 };
	io_manager manager = nt_io_manager;
	blk_t journal_size;
	blk64_t size = 0, cur;
	ext2_filsys ext2fs = NULL;
	errcode_t r;
	uint8_t* buf = NULL;
    int i, count;
    DWORD BlockSize = 0;

    if (progress_cb) progress_cb(5, "Reading device geometry...");
    printf("[EXT4] Getting device size for %s...\n", volume_name);

    r = ext2fs_get_device_size2(volume_name, KB, &size);
	if ((r != 0) || (size == 0)) {
        snprintf(errorMsg, errorMsgSize, "Could not read device size: %ld", r);
        return false;
	}
	size *= KB;
	for (i = 0; i < 5; i++) {
		if (size < ext2fs_default[i].max_size)
			break;
	}

	BlockSize = ext2fs_default[i].block_size;
	for (features.s_log_block_size = 0; EXT2_BLOCK_SIZE_BITS(&features) <= EXT2_MAX_BLOCK_LOG_SIZE; features.s_log_block_size++) {
		if (EXT2_BLOCK_SIZE(&features) == BlockSize)
			break;
	}
	features.s_log_cluster_size = features.s_log_block_size;
	size /= BlockSize;

    ext2fs_blocks_count_set(&features, size);
	ext2fs_r_blocks_count_set(&features, (blk64_t)(reserve_ratio * size));
	features.s_rev_level = 1;
	features.s_inode_size = ext2fs_default[i].inode_size;
	features.s_inodes_count = ((ext2fs_blocks_count(&features) >> ext2fs_default[i].inode_ratio) > UINT32_MAX) ?
		UINT32_MAX : (uint32_t)(ext2fs_blocks_count(&features) >> ext2fs_default[i].inode_ratio);

    ext2fs_set_feature_dir_index(&features);
	ext2fs_set_feature_filetype(&features);
	ext2fs_set_feature_large_file(&features);
	ext2fs_set_feature_sparse_super(&features);
	ext2fs_set_feature_xattr(&features);
    ext2fs_set_feature_journal(&features);
    
    features.s_default_mount_opts = EXT2_DEFM_XATTR_USER | EXT2_DEFM_ACL;

    if (progress_cb) progress_cb(10, "Initializing EXT4 superblock structures...");
    printf("[EXT4] Initializing filesystem (%llu blocks, block size %lu)...\n", size, BlockSize);

    r = ext2fs_initialize(volume_name, EXT2_FLAG_EXCLUSIVE | EXT2_FLAG_64BITS, &features, manager, &ext2fs);
	if (r != 0) {
        snprintf(errorMsg, errorMsgSize, "Could not initialize features: %ld", r);
		return false;
	}

    if (progress_cb) progress_cb(14, "Zeroing superblock and descriptors...");
    buf = (uint8_t*)calloc(16, ext2fs->io->block_size);
	r = io_channel_write_blk64(ext2fs->io, 0, 16, buf);
	free(buf);
	if (r != 0) {
        snprintf(errorMsg, errorMsgSize, "Could not zero superblock area: %ld", r);
		return false;
	}

    CoCreateGuid((GUID*)ext2fs->super->s_uuid);
	ext2fs_init_csum_seed(ext2fs);
	ext2fs->super->s_def_hash_version = EXT2_HASH_HALF_MD4;
	CoCreateGuid((GUID*)ext2fs->super->s_hash_seed);
	ext2fs->super->s_max_mnt_count = -1;
	ext2fs->super->s_creator_os = EXT2_OS_WINDOWS;
	ext2fs->super->s_errors = EXT2_ERRORS_CONTINUE;

    if (progress_cb) progress_cb(18, "Allocating block and inode bitmap tables...");
    printf("[EXT4] Allocating tables across %u block groups...\n", ext2fs->group_desc_count);

    r = ext2fs_allocate_tables(ext2fs);
	if (r != 0) {
        snprintf(errorMsg, errorMsgSize, "Could not allocate tables: %ld", r);
		return false;
	}
	r = ext2fs_convert_subcluster_bitmap(ext2fs, &ext2fs->block_map);
	if (r != 0) {
        snprintf(errorMsg, errorMsgSize, "Could not set cluster bitmap: %ld", r);
		return false;
	}

    printf("[EXT4] Zeroing inode tables across %u groups...\n", ext2fs->group_desc_count);
    for (i = 0; i < (int)ext2fs->group_desc_count; i++) {
        if (progress_cb) {
            int percent = 20 + (int)((i * 55) / ext2fs->group_desc_count);
            char msg[128];
            snprintf(msg, sizeof(msg), "Zeroing inode tables (Group %d of %d)...", i+1, ext2fs->group_desc_count);
            progress_cb(percent, msg);
        }
		cur = ext2fs_inode_table_loc(ext2fs, i);
		count = ext2fs_div_ceil((ext2fs->super->s_inodes_per_group - ext2fs_bg_itable_unused(ext2fs, i))
			* EXT2_INODE_SIZE(ext2fs->super), EXT2_BLOCK_SIZE(ext2fs->super));
		r = ext2fs_zero_blocks2(ext2fs, cur, count, &cur, &count);
		if (r != 0) {
            snprintf(errorMsg, errorMsgSize, "Could not zero inode set at position %llu (%d blocks): %ld", cur, count, r);
			return false;
		}
	}

    if (progress_cb) progress_cb(78, "Creating root directory and system entries...");
    printf("[EXT4] Creating root and lost+found directories...\n");

    r = ext2fs_mkdir(ext2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0);
	if (r != 0) {
        snprintf(errorMsg, errorMsgSize, "Failed to create root dir: %ld", r);
		return false;
	}
	ext2fs->umask = 077;
	r = ext2fs_mkdir(ext2fs, EXT2_ROOT_INO, 0, "lost+found");
	if (r != 0) {
        snprintf(errorMsg, errorMsgSize, "Failed to create 'lost+found' dir: %ld", r);
		return false;
	}

    for (i = EXT2_ROOT_INO + 1; i < (int)EXT2_FIRST_INODE(ext2fs->super); i++)
		ext2fs_inode_alloc_stats(ext2fs, i, 1);
	ext2fs_mark_ib_dirty(ext2fs);

	r = ext2fs_mark_inode_bitmap2(ext2fs->inode_map, EXT2_BAD_INO);
	if (r != 0) {
        snprintf(errorMsg, errorMsgSize, "Could not set inode bitmaps: %ld", r);
		return false;
	}
	ext2fs_inode_alloc_stats(ext2fs, EXT2_BAD_INO, 1);
	r = ext2fs_update_bb_inode(ext2fs, NULL);
	if (r != 0) {
        snprintf(errorMsg, errorMsgSize, "Could not set inode stats: %ld", r);
		return false;
	}

    journal_size = ext2fs_default_journal_size(ext2fs_blocks_count(ext2fs->super));
    journal_size /= 2;

    if (progress_cb) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Writing EXT4 journal (%u blocks) to USB...", journal_size);
        progress_cb(84, msg);
    }
    printf("[EXT4] Creating journal (%u blocks)...\n", journal_size);

    r = ext2fs_add_journal_inode(ext2fs, journal_size, EXT2_MKJOURNAL_NO_MNT_CHECK);
    if (r != 0) {
        snprintf(errorMsg, errorMsgSize, "Could not create journal: %ld", r);
        return false;
    }

    if (progress_cb) progress_cb(92, "Flushing cached metadata & syncing USB drive (almost done)...");
    printf("[EXT4] Flushing cache and finalizing filesystem...\n");
    fflush(stdout);

    r = ext2fs_close_free(&ext2fs);
	if (r != 0) {
        snprintf(errorMsg, errorMsgSize, "Failed to close and free: %ld", r);
		return false;
	}

    // Invalidate Windows Shell cache so Explorer updates drive properties
    if (strlen(volume_name) >= 6 && volume_name[0] == '\\' && volume_name[1] == '\\') {
        char rootPath[8] = { volume_name[4], ':', '\\', '\0' };
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHA, rootPath, NULL);
        SHChangeNotify(SHCNE_DRIVEADDGUI, SHCNF_PATHA, rootPath, NULL);
    }

    if (progress_cb) progress_cb(100, "Formatting completed successfully!");
    printf("[EXT4] Format completed successfully!\n");
    fflush(stdout);

    return true;
}
