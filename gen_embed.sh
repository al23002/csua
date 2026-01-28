#!/bin/sh
# Generate embedded_data.c from source files
# Usage: ./gen_embed.sh file1=name1 file2=name2 ... > embedded_data.c

echo "/* Auto-generated file - do not edit */"
echo "#include \"embedded_data.h\""
echo "#include <string.h>"
echo ""

for arg in "$@"; do
    file="${arg%%=*}"
    name="${arg#*=}"

    if [ ! -f "$file" ]; then
        echo "Error: $file not found" >&2
        exit 1
    fi

    echo "const unsigned char embedded_${name}_data[] = {"
    od -An -tx1 -v "$file" | sed 's/  */ /g; s/^ //; s/ *$//; s/[0-9a-f][0-9a-f]/0x&,/g'
    echo "};"
    echo "const int embedded_${name}_size = sizeof embedded_${name}_data;"
    echo ""
done

echo "const EmbeddedFile embedded_files[] = {"
for arg in "$@"; do
    file="${arg%%=*}"
    name="${arg#*=}"
    base=$(basename "$file")
    echo "    {\"$base\", embedded_${name}_data, embedded_${name}_size},"
done
echo "};"
echo "const int embedded_file_count = sizeof embedded_files / sizeof *embedded_files;"
echo ""
echo "const EmbeddedFile *embedded_find(const char *name)"
echo "{"
echo "    if (!name)"
echo "        return NULL;"
echo ""
echo "    for (int i = 0; i < embedded_file_count; ++i)"
echo "    {"
echo "        if (strcmp(embedded_files[i].name, name) == 0)"
echo "            return &embedded_files[i];"
echo "    }"
echo "    return NULL;"
echo "}"
