#!/bin/bash

# Define the source files in the current directory
SOURCE_DIR=$(pwd)
BLAKE2_SRC="$SOURCE_DIR/blake2.h"
HASH_TABLE_ENTRY_SRC="$SOURCE_DIR/hash_table_entry.h"
LOCKS_SRC="$SOURCE_DIR/locks.h"

# Define the target paths
BLAKE2_DST="../extern/2PC-Circuit-PSI/extern/ABY/extern/ENCRYPTO_utils/extern/relic/src/md/blake2.h"
HASH_TABLE_ENTRY_DST="../extern/2PC-Circuit-PSI/extern/HashingTables/common/hash_table_entry.h"
LOCKS_DST="../extern/2PC-Circuit-PSI/extern/EzPC/SCI/extern/SEAL/native/src/seal/util/locks.h"

# Check if source files exist
if [[ ! -f "$BLAKE2_SRC" || ! -f "$HASH_TABLE_ENTRY_SRC" || ! -f "$LOCKS_SRC" ]]; then
  echo "One or more source files are missing in the current directory."
  exit 1
fi

# Copy the files
echo "Replacing blake2.h..."
cp "$BLAKE2_SRC" "$BLAKE2_DST"

echo "Replacing hash_table_entry.h..."
cp "$HASH_TABLE_ENTRY_SRC" "$HASH_TABLE_ENTRY_DST"

echo "Replacing locks.h..."
cp "$LOCKS_SRC" "$LOCKS_DST"

echo "Files have been replaced successfully."

