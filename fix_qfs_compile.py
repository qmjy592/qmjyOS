from pathlib import Path
import sys

path = Path("fs/qfs/qfs.c")
source = path.read_text()

# qfs_create() appears before the operations table definition.
anchor = """static const struct vfs_filesystem qfs_descriptor = {
    .name = "qfs"
};
"""

declaration = anchor + """
/* Forward declaration: qfs_create() uses this operations table. */
const struct vfs_operations qfs_file_operations;
"""

if source.count(anchor) != 1:
    sys.exit("[ERROR] Cannot uniquely locate qfs_descriptor.")

if "const struct vfs_operations qfs_file_operations;" not in source:
    source = source.replace(anchor, declaration, 1)

# This project defines QFS_ERR_CORRUPT, not VFS_ERR_CORRUPT.
source = source.replace(
    "return VFS_ERR_CORRUPT;",
    "return QFS_ERR_CORRUPT;"
)

path.write_text(source)
print("[OK] QFS forward declaration added.")
print("[OK] Invalid VFS error constant corrected.")
