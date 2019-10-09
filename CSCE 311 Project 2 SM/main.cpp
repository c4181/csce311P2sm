#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <vector>

using std::cout;
using std::endl;
using std::ifstream;
using std::string;
using std::vector;

constexpr auto BUFFER_SIZE = 256;
constexpr auto SEM_STRINGS_TO_WRITE_NAME = "/sem-num-of-strings";
constexpr auto WRITE_TO_SM_NAME = "/write-perm";
constexpr auto LOOP_SEM_NAME = "/loop";
constexpr auto SHARED_MEMORY_NAME = "/find-words";

/***************************************************************************
 * Author/copyright:  Christopher Moyer.  All rights reserved.
 * Date: 7 October 2019
 *
 * This program takes in a text file and a word and outputs any matches to
 * stdout. Parent process handles all input/output and child process
 * handles parsing the file for matches. All information is passed
 * between processes using shared memory. Data is processed using map/reduce.
 *
 **/

int main(int argc, char *argv[]) {
  int n1 = fork();
  void *shared_mem_ptr;
  int fd_shm;
  char buffer[BUFFER_SIZE];
  vector<string> lines;
  sem_t *num_of_strings, *write_perm, *continue_loop;

  // Parent Process
  if (n1 > 0) {
    string line;
    ifstream file(argv[1]);

    // Read all file lines into a vector
    while (file.eof() == false) {
      getline(file, line);
      if (line.empty()) continue;
      lines.push_back(line);
    }
    file.close();

    // Open Shared Memory
    fd_shm = shm_open(SHARED_MEMORY_NAME, O_CREAT | O_RDWR, 0);
    if (fd_shm == -1) cout << "Error Opening SHM: " << errno << endl;

    // Map Memory
    shared_mem_ptr = mmap(nullptr, sizeof(buffer), PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd_shm, 0);
    if (shared_mem_ptr == MAP_FAILED)
      cout << "Error Mapping Memory: " << errno << endl;

    // Open Semaphores

    num_of_strings = sem_open(SEM_STRINGS_TO_WRITE_NAME, O_CREAT, 0660, 0);
    if (num_of_strings == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    write_perm = sem_open(WRITE_TO_SM_NAME, O_CREAT, 0660, 0);

    continue_loop = sem_open(LOOP_SEM_NAME, O_CREAT, 0660, 1);

    // Critical Section
    for (size_t i = 0; i < lines.size(); ++i) {
      sem_wait(continue_loop);
      sem_post(num_of_strings);
      memset(buffer, 0, sizeof(buffer));
      strcpy(buffer, lines.at(i).c_str());
      memcpy(shared_mem_ptr, buffer, sizeof(buffer));
      sem_post(write_perm);
    }
    sem_post(write_perm);
    // End Critical Section

    wait(NULL);
    //return (0);
  }

  // Child Process
  if (n1 == 0) {
    // Open and Create Shared Memory
    fd_shm = shm_open(SHARED_MEMORY_NAME, O_RDWR | O_CREAT, 0660);
    if (fd_shm == -1) cout << "Error Creating Shared Memory: " << errno << endl;

    // Set Size of Shared Memory
    ftruncate(fd_shm, sizeof(buffer));

    // Open and Create Semaphores
    write_perm = sem_open(WRITE_TO_SM_NAME, O_CREAT, 0660, 0);
    if (write_perm == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    num_of_strings = sem_open(SEM_STRINGS_TO_WRITE_NAME, O_CREAT, 0660, 0);

    continue_loop = sem_open(LOOP_SEM_NAME, O_CREAT, 0660, 1);

    shared_mem_ptr = mmap(nullptr, sizeof(buffer), PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd_shm, 0);
    if (shared_mem_ptr == MAP_FAILED)
      cout << "Error Mapping Memory: " << errno << endl;
    
    int i;
    while (i != -1) {
      // Critial Section
      sem_wait(write_perm);
      memset(buffer, 0, sizeof(buffer));
      strcpy(buffer, (char *)shared_mem_ptr);
      lines.push_back(string(buffer));
      cout << string(buffer) << endl;
      i = sem_trywait(num_of_strings);
      sem_post(continue_loop);
      // End Critical Section
    }

    return (0);
  }
}