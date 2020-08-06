## CSE

**Introduction**

Using LLVM, I develop algorithms for Common Subexpression Elimination, Constant Folding, and Dead Code elimination.

**Results**

Optimizations are recorded by incrementing output counters *CSE_Dead*, *CSE_Basic*, and *CSE_Simplify*. These variables displayed the number of dead code eliminations, constant foldings, and subexpression eliminations respectively after running these optimization files over any given piece of code. An example output on terminal after optimizations are done is as shown:

![Alt text](/CSE/images/readmeimg1.png?raw=true "terminal_optimizations")

Where different optimizations done to the code are recorded by each output counter.

## C--
**Introduction**

Using C++, I wrote a script that can take any C-- program and create an LLVM bitcode file.

**Features** 

Some features that differentiate C-- from C and C++ include:
- lack of function prototypes
- no support for type checking and type interference
- int (i64) is only supported
- global variable and function definitions

**Example**

A sample piece of C-- code that my script would create LLVM bitcode for:

![Alt text](/C--/images/readmeimg3.png?raw=true "LLVM_IR_code")

A piece of my cmm.cpp file that would be called for **while (j < 10)** *(line numbers included for convenience):*

![Alt text](/C--/images/readmeimg4.png?raw=true "LLVM_IR_code")

![Alt text](/C--/images/readmeimg8.png?raw=true "LLVM_IR_code")

![Alt text](/C--/images/readmeimg6.png?raw=true "LLVM_IR_code")

![Alt text](/C--/images/readmeimg7.png?raw=true "LLVM_IR_code")
