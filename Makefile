CC = clang
CFLAGS ?= -g -DDEBUG
CFLAGS += -std=c23

TARGET = codegen

OBJS = parser.o preprocessor.o scanner.o keyword.o create.o util.o definitions.o compiler.o cminor_type.o parsed_type.o meanvisitor.o header_decl_visitor.o header_store.o header_index.o constant_pool.o method_code.o code_output.o codebuilder_core.o codebuilder_types.o codebuilder_frame.o codebuilder_label.o codebuilder_control.o codebuilder_part1.o codebuilder_part2.o codebuilder_part3.o codebuilder_stackmap.o codebuilder_ptr.o codebuilder_internal.o classfile_opcode.o cfg.o classfile.o codegen_constants.o codegen_symbols.o codegen_jvm_types.o codegenvisitor.o codegenvisitor_expr_ops.o codegenvisitor_expr_values.o codegenvisitor_expr_assign.o codegenvisitor_expr_complex.o codegenvisitor_expr_util.o codegenvisitor_util.o codegenvisitor_stmt_basic.o codegenvisitor_stmt_control.o codegenvisitor_stmt_switch_jump.o codegenvisitor_stmt_decl.o codegenvisitor_stmt_util.o synthetic_codegen.o visitor.o ascii.o

# Embedded data files (source=symbol_name)
EMBED_FILES = \
		my/limits.c=limits \
		my/limits.h=limits_h \
		my/stdarg.h=stdarg_h \
		my/stddef.h=stddef_h \
		my/stdint.h=stdint_h \
		my/stdio.c=stdio \
		my/stdio.h=stdio_h \
		my/stdlib.c=stdlib \
		my/stdlib.h=stdlib_h \
		my/string.c=string \
		my/string.h=string_h

EMBED_SOURCES = $(foreach f,$(EMBED_FILES),$(word 1,$(subst =, ,$f)))

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(OBJS) embedded_data.o codegen.o
	$(CC) -o $@ $^

parser.c: parser.y
	bison -d -o $@ $^

	@sed -E 's/^([[:space:]]*)#[[:space:]]*include([[:space:]]*)([<"])/\1__KEEP_INCLUDE__\2\3/g' $@ \
	| sed 's/#define YYDEBUG 1/#define YYDEBUG 0/' \
	| $(CC) $(CFLAGS) -E -P -x c -DYY_ATTRIBUTE_UNUSED= - \
	| sed -E 's/^([[:space:]]*)__KEEP_INCLUDE__([[:space:]]*)([<"])/\1#include\2\3/g' \
	| sed -E 's/\(\(void\) \(([a-zA-Z_][a-zA-Z0-9_]*)\)\)/(void)\1/g' \
	| sed 's/((void) 0);/(void)0;/g' \
	| sed '/((void) (0 &&/d' \
	| sed 's/\[+yyssp/[yyssp/g' \
	| sed 's/long int/long/g' \
	| sed 's/(yyloc)\./yyloc./g' \
	| sed 's/(yyval)\./yyval./g' \
	| sed -E 's/\(yyval\.([a-zA-Z_][a-zA-Z0-9_]*)\)/yyval.\1/g' \
	| sed -E 's/\(yyvsp\[([^]]*)\]\.([a-zA-Z_][a-zA-Z0-9_]*)\)/yyvsp[\1].\2/g' \
	| sed 's/^  (yyvsp -= (yylen), yyssp -= (yylen), yylsp -= (yylen));/  yyvsp = yyvsp - yylen; yyssp = yyssp - yylen; yylsp = yylsp - yylen;/g' \
	| sed 's/(yyvsp -= (1), yyssp -= (1), yylsp -= (1));/yyvsp = yyvsp - 1; yyssp = yyssp - 1; yylsp = yylsp - 1;/g' \
	| sed 's/(\*yylsp)\./yylsp->/g' \
	| sed 's/((yyerror_range)\[\([0-9]\)\])\./yyerror_range[\1]./g' \
	| sed -E 's/!\(\(yyn\) == \((-[0-9]+)\)\)/yyn != \1/g' \
	| sed -E 's/\(\(yyn\) == \((-[0-9]+)\)\)/yyn == \1/g' \
	| sed 's/\*++yyvsp = /yyvsp = yyvsp + 1; *yyvsp = /g' \
	| sed 's/\*++yylsp = /yylsp = yylsp + 1; *yylsp = /g' \
	| sed 's/yyssp++;/yyssp = yyssp + 1;/g' \
	| sed 's/++yylsp;/yylsp = yylsp + 1;/g' \
	| sed 's/def->id.type_index = compiler->enum_type_counter++;/def->id.type_index = compiler->enum_type_counter; compiler->enum_type_counter = compiler->enum_type_counter + 1;/g' \
	| sed 's/def->member_count++;/def->member_count = def->member_count + 1;/g' \
	| sed 's/yyerrstatus--;/yyerrstatus = yyerrstatus - 1;/g' \
	| sed 's/++yynerrs;/yynerrs = yynerrs + 1;/g' > $@.tmp

	mv $@.tmp $@

# Generate embedded_data.c from source files
embedded_data.c: $(EMBED_SOURCES) gen_embed.sh
	sh gen_embed.sh $(EMBED_FILES) > $@

clean:
	rm -rf *.o $(TARGET)
	rm -rf embedded_data.c
	rm -rf *.class *.jar out*

BOOTSTRAP_JAR ?= codegen.jar

.PHONY: jar jar1 jar2
jar: codegen.jar
jar1: codegen1.jar
jar2: codegen2.jar

codegen.jar: $(TARGET)
	mkdir -p out
	cd out && ../$(TARGET) ../codegen.c && \
	find . -type f -name '*.class' -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum
	jar --create --file codegen.jar --main-class codegen -C out .

codegen1.jar: $(BOOTSTRAP_JAR) parser.c embedded_data.c
	mkdir -p out1
	cd out1 && java -jar ../$< ../codegen.c && \
	find . -type f -name '*.class' -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum
	jar --create --file codegen1.jar --main-class codegen -C out1 .

codegen2.jar: codegen1.jar
	mkdir -p out2
	cd out2 && java -jar ../$< ../codegen.c && \
	find . -type f -name '*.class' -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum
	jar --create --file codegen2.jar --main-class codegen -C out2 .
