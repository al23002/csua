### Build and run the compiler:

C-native build:
```
make clean ; make codegen
./codegen test/java_call.c
java java_call
```

Java self-hosted build
```
make clean ; make codegen
./codegen codegen.c
java codegen test/java_call.c
java java_call
```

### Run the next self-hosting stage (takes a while)

Starting from the C-native compiler
```
make clean ; make jar2
```

Starting from a precompiled Java build (fewer dependencies)
```
make clean ; make jar2 BOOTSTRAP_JAR=sample.jar1
```
