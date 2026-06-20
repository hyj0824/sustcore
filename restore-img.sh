#!/bin/bash
# Restore clean ext4 test image from backup
IMG="alpine-linux-riscv64-ext4fs.img"
BAK="${IMG}.bak"

cd "$(dirname "$0")"

if [ ! -f "$BAK" ]; then
    echo "Creating backup from current image: $BAK"
    cp "$IMG" "$BAK"
    echo "Backup created. Image is already clean."
    exit 0
fi

echo "Restoring $IMG from $BAK ..."
cp "$BAK" "$IMG"
echo "Done."
