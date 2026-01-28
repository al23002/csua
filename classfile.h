/*
 * Java Class File Format Generator
 *
 * Generates valid Java .class files that can be inspected with javap.
 * Based on Java SE specification for the class file format.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * Constant Pool Tags (JVM Spec §4.4)
 */
typedef enum
{
    CP_TAG_UTF8 = 1,
    CP_TAG_INTEGER = 3,
    CP_TAG_FLOAT = 4,
    CP_TAG_LONG = 5,
    CP_TAG_DOUBLE = 6,
    CP_TAG_CLASS = 7,
    CP_TAG_STRING = 8,
    CP_TAG_FIELDREF = 9,
    CP_TAG_METHODREF = 10,
    CP_TAG_INTERFACE_METHODREF = 11,
    CP_TAG_NAME_AND_TYPE = 12,
    CP_TAG_METHOD_HANDLE = 15,
    CP_TAG_METHOD_TYPE = 16,
    CP_TAG_INVOKE_DYNAMIC = 18,
} CF_ConstantTag;

/*
 * Access Flags (JVM Spec §4.1, §4.5, §4.6)
 */
typedef enum
{
    ACC_PUBLIC = 0x0001,
    ACC_PRIVATE = 0x0002,
    ACC_PROTECTED = 0x0004,
    ACC_STATIC = 0x0008,
    ACC_FINAL = 0x0010,
    ACC_SUPER = 0x0020,        /* For classes */
    ACC_SYNCHRONIZED = 0x0020, /* For methods */
    ACC_VOLATILE = 0x0040,
    ACC_BRIDGE = 0x0040,
    ACC_TRANSIENT = 0x0080,
    ACC_VARARGS = 0x0080,
    ACC_NATIVE = 0x0100,
    ACC_INTERFACE = 0x0200,
    ACC_ABSTRACT = 0x0400,
    ACC_STRICT = 0x0800,
    ACC_SYNTHETIC = 0x1000,
    ACC_ANNOTATION = 0x2000,
    ACC_ENUM = 0x4000,
} CF_AccessFlags;

/*
 * Constant Pool Entry
 */
typedef struct CF_ConstantEntry_tag
{
    CF_ConstantTag tag;
    union
    {
        /* CP_TAG_UTF8 */
        struct
        {
            uint16_t length;
            uint8_t *bytes;
        } utf8;

        /* CP_TAG_INTEGER */
        int32_t integer;

        /* CP_TAG_FLOAT */
        float float_val;

        /* CP_TAG_LONG */
        int64_t long_val;

        /* CP_TAG_DOUBLE */
        double double_val;

        /* CP_TAG_CLASS, CP_TAG_STRING, CP_TAG_METHOD_TYPE */
        uint16_t index;

        /* CP_TAG_FIELDREF, CP_TAG_METHODREF, CP_TAG_INTERFACE_METHODREF */
        struct
        {
            uint16_t class_index;
            uint16_t name_and_type_index;
        } ref;

        /* CP_TAG_NAME_AND_TYPE */
        struct
        {
            uint16_t name_index;
            uint16_t descriptor_index;
        } name_and_type;

        /* CP_TAG_METHOD_HANDLE */
        struct
        {
            uint8_t reference_kind;
            uint16_t reference_index;
        } method_handle;

        /* CP_TAG_INVOKE_DYNAMIC */
        struct
        {
            uint16_t bootstrap_method_attr_index;
            uint16_t name_and_type_index;
        } invoke_dynamic;
    } u;
} CF_ConstantEntry;

/*
 * Constant Pool Builder
 */
typedef struct CF_ConstantPool_tag
{
    CF_ConstantEntry *entries;
    uint16_t count;
    uint16_t capacity;
} CF_ConstantPool;

/*
 * Exception Table Entry (for Code attribute)
 */
typedef struct CF_ExceptionEntry_tag
{
    uint16_t start_pc;
    uint16_t end_pc;
    uint16_t handler_pc;
    uint16_t catch_type;
} CF_ExceptionEntry;

/*
 * StackMapTable structures (JVM Spec §4.7.4)
 */
typedef enum
{
    CF_VERIFICATION_TOP = 0,
    CF_VERIFICATION_INTEGER = 1,
    CF_VERIFICATION_FLOAT = 2,
    CF_VERIFICATION_DOUBLE = 3,
    CF_VERIFICATION_LONG = 4,
    CF_VERIFICATION_NULL = 5,
    CF_VERIFICATION_UNINITIALIZED_THIS = 6,
    CF_VERIFICATION_OBJECT = 7,
    CF_VERIFICATION_UNINITIALIZED = 8,
} CF_VerificationTypeTag;

typedef struct CF_VerificationTypeInfo_tag
{
    CF_VerificationTypeTag tag;
    union
    {
        /* CF_VERIFICATION_OBJECT */
        uint16_t cpool_index;
        /* CF_VERIFICATION_UNINITIALIZED */
        uint16_t offset;
    } u;
} CF_VerificationTypeInfo;

typedef struct CF_StackMapFrame_tag
{
    uint8_t frame_type;
    uint16_t offset_delta;
    uint16_t locals_count;
    CF_VerificationTypeInfo *locals;
    uint16_t stack_count;
    CF_VerificationTypeInfo *stack;
} CF_StackMapFrame;

/*
 * Attribute (generic structure)
 */
typedef struct CF_Attribute_tag
{
    uint16_t attribute_name_index;
    uint32_t attribute_length;
    uint8_t *info;
} CF_Attribute;

/*
 * LineNumberTable Entry (JVM Spec §4.7.12)
 */
typedef struct CF_LineNumberEntry_tag
{
    int start_pc;
    int line_number;
} CF_LineNumberEntry;

/*
 * Code Attribute
 */
typedef struct CF_CodeAttribute_tag
{
    uint16_t attribute_name_index;
    uint16_t max_stack;
    uint16_t max_locals;
    uint32_t code_length;
    uint8_t *code;
    uint16_t exception_table_length;
    CF_ExceptionEntry *exception_table;
    uint16_t attributes_count;
    CF_Attribute *attributes;

    /* Optional StackMapTable (Java SE verification frames) */
    bool has_stack_map_table;
    uint16_t stack_map_table_name_index;
    uint16_t stack_map_frame_count;
    CF_StackMapFrame *stack_map_frames;

    /* Optional LineNumberTable (for debugging) */
    bool has_line_number_table;
    uint16_t line_number_table_name_index;
    uint16_t line_number_count;
    CF_LineNumberEntry *line_numbers;
} CF_CodeAttribute;

/*
 * Field Info
 */
typedef struct CF_FieldInfo_tag
{
    uint16_t access_flags;
    uint16_t name_index;
    uint16_t descriptor_index;
    uint16_t attributes_count;
    CF_Attribute *attributes;
} CF_FieldInfo;

/*
 * Method Info
 */
typedef struct CF_MethodInfo_tag
{
    uint16_t access_flags;
    uint16_t name_index;
    uint16_t descriptor_index;
    uint16_t attributes_count;
    CF_Attribute *attributes;
    CF_CodeAttribute *code; /* Convenience pointer if Code attribute exists */
} CF_MethodInfo;

/*
 * Complete Class File Structure
 */
typedef struct CF_ClassFile_tag
{
    uint32_t magic;
    uint16_t minor_version;
    uint16_t major_version;

    CF_ConstantPool *constant_pool;

    uint16_t access_flags;
    uint16_t this_class;
    uint16_t super_class;

    uint16_t interfaces_count;
    uint16_t *interfaces;

    uint16_t fields_count;
    CF_FieldInfo *fields;

    uint16_t methods_count;
    CF_MethodInfo *methods;

    uint16_t attributes_count;
    CF_Attribute *attributes;

    /* Optional SourceFile attribute for debugging */
    bool has_source_file;
    uint16_t source_file_name_index;
} CF_ClassFile;

/*
 * Class File Builder Context
 */
typedef struct CF_Builder_tag
{
    CF_ClassFile *cf;

    /* Cached constant pool indices for common entries */
    uint16_t code_attr_name_index;
    uint16_t stackmap_attr_name_index;
    uint16_t this_class_name_index;
    uint16_t super_class_name_index;
} CF_Builder;

/* ============================================================
 * Constant Pool Operations
 * ============================================================ */

/* Create constant pool (factory) */
CF_ConstantPool *cf_cp_create();

/* Free constant pool resources */
void cf_cp_free(CF_ConstantPool *cp);

/* Add UTF-8 constant, returns index (1-based) */
int cf_cp_add_utf8(CF_ConstantPool *cp, const char *str);

/* Add UTF-8 constant with explicit length (supports embedded nulls) */
int cf_cp_add_utf8_len(CF_ConstantPool *cp, const uint8_t *data, int len);

/* Add Integer constant */
int cf_cp_add_integer(CF_ConstantPool *cp, int32_t value);

/* Add Long constant (uses two slots) */
int cf_cp_add_long(CF_ConstantPool *cp, int64_t value);

/* Add Float constant */
int cf_cp_add_float(CF_ConstantPool *cp, float value);

/* Add Double constant (uses two slots) */
int cf_cp_add_double(CF_ConstantPool *cp, double value);

/* Add Class reference (name is internal format, e.g., "java/lang/Object") */
int cf_cp_add_class(CF_ConstantPool *cp, const char *name);

/* Add String constant */
int cf_cp_add_string(CF_ConstantPool *cp, const char *str);

/* Add String constant with explicit length (supports embedded nulls) */
int cf_cp_add_string_len(CF_ConstantPool *cp, const uint8_t *data, int len);

/* Add NameAndType */
int cf_cp_add_name_and_type(CF_ConstantPool *cp,
                            const char *name, const char *descriptor);

/* Add Fieldref */
int cf_cp_add_fieldref(CF_ConstantPool *cp,
                       const char *class_name,
                       const char *field_name,
                       const char *descriptor);

/* Add Methodref */
int cf_cp_add_methodref(CF_ConstantPool *cp,
                        const char *class_name,
                        const char *method_name,
                        const char *descriptor);

/* Find existing UTF-8 entry, returns 0 if not found */
int cf_cp_find_utf8(CF_ConstantPool *cp, const char *str);

/* ============================================================
 * Builder Operations
 * ============================================================ */

/* Create builder with class name (factory) */
CF_Builder *cf_builder_create(const char *class_name);

/* Create builder with existing constant pool (takes ownership) */
CF_Builder *cf_builder_create_from_cp(const char *class_name, CF_ConstantPool *cp);

/* Destroy builder and free resources */
void cf_builder_destroy(CF_Builder *builder);

/* Write class file to disk */
bool cf_builder_write_to_file(CF_Builder *builder, const char *filename);

/* Set superclass (default is java/lang/Object) */
void cf_builder_set_super(CF_Builder *builder, const char *super_name);

/* Add a static field */
void cf_builder_add_field(CF_Builder *builder,
                          int access_flags,
                          const char *name,
                          const char *descriptor);

/* Begin a new method, returns method index */
int cf_builder_begin_method(CF_Builder *builder,
                            int access_flags,
                            const char *name,
                            const char *descriptor);

/* Set Code attribute for current method */
void cf_builder_set_code(CF_Builder *builder,
                         int method_index,
                         int max_stack,
                         int max_locals,
                         uint8_t *code,
                         int code_length);

/* Set StackMapTable for a method's Code attribute */
void cf_builder_set_stack_map_table(CF_Builder *builder,
                                    int method_index,
                                    CF_StackMapFrame *frames,
                                    int frame_count);

/* Set LineNumberTable for a method's Code attribute */
void cf_builder_set_line_number_table(CF_Builder *builder,
                                      int method_index,
                                      CF_LineNumberEntry *entries,
                                      int entry_count);

/* Set SourceFile attribute for the class (for debugging) */
void cf_builder_set_source_file(CF_Builder *builder, const char *source_file);

/* ============================================================
 * Output Operations
 * ============================================================ */

/* Write class file to buffer, returns total size */
int cf_write_to_buffer(CF_ClassFile *cf, uint8_t **buffer);

/* Write class file to file */
bool cf_write_to_file(CF_ClassFile *cf, const char *filename);

/* ============================================================
 * Descriptor Utilities
 * ============================================================ */

/* Build method descriptor from return type and parameter types */
/* e.g., cf_build_method_descriptor("I", "II") returns "(II)I" */
char *cf_build_method_descriptor(const char *return_type, const char *param_types);

/* Type descriptors for common types */
extern const char *CF_DESC_VOID;
extern const char *CF_DESC_INT;
extern const char *CF_DESC_LONG;
extern const char *CF_DESC_FLOAT;
extern const char *CF_DESC_DOUBLE;
extern const char *CF_DESC_BYTE;
extern const char *CF_DESC_CHAR;
extern const char *CF_DESC_SHORT;
extern const char *CF_DESC_BOOLEAN;

/* Build array type descriptor */
/* e.g., cf_desc_array("I") returns "[I" */
char *cf_desc_array(const char *element_type);

/* Build object type descriptor */
/* e.g., cf_desc_object("java/lang/String") returns "Ljava/lang/String;" */
char *cf_desc_object(const char *class_name);

/* ============================================================
 * JVM Opcodes (JVM Spec §6.5)
 * ============================================================ */

typedef enum
{
    CF_NOP = 0,
    CF_ACONST_NULL = 1,
    CF_ICONST_M1 = 2,
    CF_ICONST_0 = 3,
    CF_ICONST_1 = 4,
    CF_ICONST_2 = 5,
    CF_ICONST_3 = 6,
    CF_ICONST_4 = 7,
    CF_ICONST_5 = 8,
    CF_LCONST_0 = 9,
    CF_LCONST_1 = 10,
    CF_FCONST_0 = 11,
    CF_FCONST_1 = 12,
    CF_FCONST_2 = 13,
    CF_DCONST_0 = 14,
    CF_DCONST_1 = 15,
    CF_BIPUSH = 16,
    CF_SIPUSH = 17,
    CF_LDC = 18,
    CF_LDC_W = 19,
    CF_LDC2_W = 20,
    CF_ILOAD = 21,
    CF_LLOAD = 22,
    CF_FLOAD = 23,
    CF_DLOAD = 24,
    CF_ALOAD = 25,
    CF_ILOAD_0 = 26,
    CF_ILOAD_1 = 27,
    CF_ILOAD_2 = 28,
    CF_ILOAD_3 = 29,
    CF_LLOAD_0 = 30,
    CF_LLOAD_1 = 31,
    CF_LLOAD_2 = 32,
    CF_LLOAD_3 = 33,
    CF_FLOAD_0 = 34,
    CF_FLOAD_1 = 35,
    CF_FLOAD_2 = 36,
    CF_FLOAD_3 = 37,
    CF_DLOAD_0 = 38,
    CF_DLOAD_1 = 39,
    CF_DLOAD_2 = 40,
    CF_DLOAD_3 = 41,
    CF_ALOAD_0 = 42,
    CF_ALOAD_1 = 43,
    CF_ALOAD_2 = 44,
    CF_ALOAD_3 = 45,
    CF_IALOAD = 46,
    CF_LALOAD = 47,
    CF_FALOAD = 48,
    CF_DALOAD = 49,
    CF_AALOAD = 50,
    CF_BALOAD = 51,
    CF_CALOAD = 52,
    CF_SALOAD = 53,
    CF_ISTORE = 54,
    CF_LSTORE = 55,
    CF_FSTORE = 56,
    CF_DSTORE = 57,
    CF_ASTORE = 58,
    CF_ISTORE_0 = 59,
    CF_ISTORE_1 = 60,
    CF_ISTORE_2 = 61,
    CF_ISTORE_3 = 62,
    CF_LSTORE_0 = 63,
    CF_LSTORE_1 = 64,
    CF_LSTORE_2 = 65,
    CF_LSTORE_3 = 66,
    CF_FSTORE_0 = 67,
    CF_FSTORE_1 = 68,
    CF_FSTORE_2 = 69,
    CF_FSTORE_3 = 70,
    CF_DSTORE_0 = 71,
    CF_DSTORE_1 = 72,
    CF_DSTORE_2 = 73,
    CF_DSTORE_3 = 74,
    CF_ASTORE_0 = 75,
    CF_ASTORE_1 = 76,
    CF_ASTORE_2 = 77,
    CF_ASTORE_3 = 78,
    CF_IASTORE = 79,
    CF_LASTORE = 80,
    CF_FASTORE = 81,
    CF_DASTORE = 82,
    CF_AASTORE = 83,
    CF_BASTORE = 84,
    CF_CASTORE = 85,
    CF_SASTORE = 86,
    CF_POP = 87,
    CF_POP2 = 88,
    CF_DUP = 89,
    CF_DUP_X1 = 90,
    CF_DUP_X2 = 91,
    CF_DUP2 = 92,
    CF_DUP2_X1 = 93,
    CF_DUP2_X2 = 94,
    CF_SWAP = 95,
    CF_IADD = 96,
    CF_LADD = 97,
    CF_FADD = 98,
    CF_DADD = 99,
    CF_ISUB = 100,
    CF_LSUB = 101,
    CF_FSUB = 102,
    CF_DSUB = 103,
    CF_IMUL = 104,
    CF_LMUL = 105,
    CF_FMUL = 106,
    CF_DMUL = 107,
    CF_IDIV = 108,
    CF_LDIV = 109,
    CF_FDIV = 110,
    CF_DDIV = 111,
    CF_IREM = 112,
    CF_LREM = 113,
    CF_FREM = 114,
    CF_DREM = 115,
    CF_INEG = 116,
    CF_LNEG = 117,
    CF_FNEG = 118,
    CF_DNEG = 119,
    CF_ISHL = 120,
    CF_LSHL = 121,
    CF_ISHR = 122,
    CF_LSHR = 123,
    CF_IUSHR = 124,
    CF_LUSHR = 125,
    CF_IAND = 126,
    CF_LAND = 127,
    CF_IOR = 128,
    CF_LOR = 129,
    CF_IXOR = 130,
    CF_LXOR = 131,
    CF_IINC = 132,
    CF_I2L = 133,
    CF_I2F = 134,
    CF_I2D = 135,
    CF_L2I = 136,
    CF_L2F = 137,
    CF_L2D = 138,
    CF_F2I = 139,
    CF_F2L = 140,
    CF_F2D = 141,
    CF_D2I = 142,
    CF_D2L = 143,
    CF_D2F = 144,
    CF_I2B = 145,
    CF_I2C = 146,
    CF_I2S = 147,
    CF_LCMP = 148,
    CF_FCMPL = 149,
    CF_FCMPG = 150,
    CF_DCMPL = 151,
    CF_DCMPG = 152,
    CF_IFEQ = 153,
    CF_IFNE = 154,
    CF_IFLT = 155,
    CF_IFGE = 156,
    CF_IFGT = 157,
    CF_IFLE = 158,
    CF_IF_ICMPEQ = 159,
    CF_IF_ICMPNE = 160,
    CF_IF_ICMPLT = 161,
    CF_IF_ICMPGE = 162,
    CF_IF_ICMPGT = 163,
    CF_IF_ICMPLE = 164,
    CF_IF_ACMPEQ = 165,
    CF_IF_ACMPNE = 166,
    CF_GOTO = 167,
    CF_JSR = 168,
    CF_RET = 169,
    CF_TABLESWITCH = 170,
    CF_LOOKUPSWITCH = 171,
    CF_IRETURN = 172,
    CF_LRETURN = 173,
    CF_FRETURN = 174,
    CF_DRETURN = 175,
    CF_ARETURN = 176,
    CF_RETURN = 177,
    CF_GETSTATIC = 178,
    CF_PUTSTATIC = 179,
    CF_GETFIELD = 180,
    CF_PUTFIELD = 181,
    CF_INVOKEVIRTUAL = 182,
    CF_INVOKESPECIAL = 183,
    CF_INVOKESTATIC = 184,
    CF_INVOKEINTERFACE = 185,
    CF_INVOKEDYNAMIC = 186,
    CF_NEW = 187,
    CF_NEWARRAY = 188,
    CF_ANEWARRAY = 189,
    CF_ARRAYLENGTH = 190,
    CF_ATHROW = 191,
    CF_CHECKCAST = 192,
    CF_INSTANCEOF = 193,
    CF_MONITORENTER = 194,
    CF_MONITOREXIT = 195,
    CF_WIDE = 196,
    CF_MULTIANEWARRAY = 197,
    CF_IFNULL = 198,
    CF_IFNONNULL = 199,
    CF_GOTO_W = 200,
    CF_JSR_W = 201,
} CF_Opcode;

static const int CF_MAX_OPCODE = CF_JSR_W;

/* ============================================================
 * JVM Value Types
 * ============================================================ */

typedef enum
{
    CF_VAL_INT,
    CF_VAL_LONG,
    CF_VAL_FLOAT,
    CF_VAL_DOUBLE,
    CF_VAL_OBJECT,
    CF_VAL_NULL
} CF_ValueTag;
