## EnkiTS Examples

This directory contains example programs that demonstrate the use of EnkiTS. Each example is built as a separate executable.

### Building the Examples

To build the examples, you need to have CMake installed. You can build the examples using two separate ways:

#### Building with the entire EnkiTS project

1. Clone the EnkiTS repository:

```bash
git clone https://github.com/dougbinks/enkiTS.git
cd enkiTS/
```

2. Setup CMake with the examples enabled:

```bash
cmake -S . -B build -DENKITS_BUILD_EXAMPLES=ON
```

3. Build the project and examples:

```bash
cmake --build build
```

4. Run the examples:

```bash
cd build/examples
./ParallelSum
./PinnedTask
./Priorities
./ExternalTaskThread
...
```

#### Building with EnkiTS installed in your system

Optionally, in case EnkiTS is installed in your system already, you can build the examples without building the entire EnkiTS project.

1. Clone the EnkiTS repository:

```bash
git clone https://github.com/dougbinks/enkiTS.git
cd enkiTS/example
```

2. Setup CMake examples to find the installed EnkiTS package:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=</path/to/enkiTS/installation>
```

3. Build the examples:

```bash
cmake --build build
```

4. Run the examples:

```bash
cd build/examples
./ParallelSum
./PinnedTask
./Priorities
...
```
