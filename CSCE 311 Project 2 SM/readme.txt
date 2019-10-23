This program takes in a text file and a word and outputs any matches to
stdout. Parent process handles all input/output and child process
handles parsing the file for matches. All information is passed
between processes using shared memory. Data is processed using map/reduce.

Usage:
 ./a.out *textFile* *word*

Compilation:
  Use the included make file or ensure you compile with -lrt and -lpthread
  tags in g++ using the c++ 11 standard.
