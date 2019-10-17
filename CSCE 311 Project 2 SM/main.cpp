#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <vector>

using std::cout;
using std::endl;
using std::ifstream;
using std::string;
using std::regex;
using std::regex_search;
using std::regex_constants::icase;
using std::vector;

constexpr auto BUFFER_SIZE = 256;
constexpr auto SEM_STRINGS_TO_WRITE_NAME = "/sem-num-of-strings";
constexpr auto WRITE_TO_SM_NAME = "/write-perm";
constexpr auto LOOP_SEM_NAME = "/loop";
constexpr auto SHARED_MEMORY_NAME = "/find-words";

constexpr auto S3_NAME = "/s3";
constexpr auto S4_NAME = "/s4";
constexpr auto S5_NAME = "/s5";

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

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

struct thread_args {
  vector<string> &text_lines;
  vector<string> &text_matching_lines;
  int start_index;
  int stop_index;
  string word;
};

void *ParseLine(void* targs) {
  struct thread_args *tdata;
  tdata = (struct thread_args *) targs;
  string regex_string = "\\b" + tdata -> word + "\\b";
  regex e(regex_string, icase);
  for (int i = tdata -> start_index; i < tdata -> stop_index; ++i) {
    if (regex_search(tdata -> text_lines.at(i), e)) {
      // Critical Section
      pthread_mutex_lock(&lock);
      tdata -> text_matching_lines.push_back(tdata -> text_lines.at(i));
      pthread_mutex_unlock(&lock);
      // End Critical Section
    }
  }
  return nullptr;
}

int main(int argc, char *argv[]) {
  int n1 = fork();
  void *shared_mem_ptr;
  int fd_shm;
  char buffer[BUFFER_SIZE];
  vector<string> lines, matching_lines;
  sem_t *num_of_strings, *write_perm, *continue_loop, *s3, *s4, *s5;

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
    if (write_perm == SEM_FAILED)
     cout << "Error Creating Semaphore: " << errno << endl;

    continue_loop = sem_open(LOOP_SEM_NAME, O_CREAT, 0660, 1);
    if (continue_loop == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    s3 = sem_open(S3_NAME, O_CREAT, 0660, 0);
    if (s3 == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    s4 = sem_open(S4_NAME, O_CREAT, 0660, 1);
    if (s4 == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    s5 = sem_open(S5_NAME, O_CREAT, 0660, 0);
    if (s5 == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    // Unlink Semaphores
    sem_unlink(SEM_STRINGS_TO_WRITE_NAME);
    sem_unlink(WRITE_TO_SM_NAME);
    sem_unlink(LOOP_SEM_NAME);
    sem_unlink(S3_NAME);
    sem_unlink(S4_NAME);
    sem_unlink(S5_NAME);

    for (size_t i = 0; i < lines.size() - 1; ++i) {
      sem_post(num_of_strings);
    }

    // Pass all file lines to child through shared memory
    // Critical Section
    for (size_t i = 0; i < lines.size(); ++i) {
      sem_wait(continue_loop);
      memset(buffer, 0, sizeof(buffer));
      strcpy(buffer, lines.at(i).c_str());
      memcpy(shared_mem_ptr, buffer, sizeof(buffer));
      sem_post(write_perm);
    }
    // End Critical Section

    // Read all results from shared memory into a vector
    int i = 0;
    while (i != -1) {
      // Critical Section
      sem_wait(s3);
      memset(buffer, 0, sizeof(buffer));
      strcpy(buffer, static_cast<char*>(shared_mem_ptr));
      matching_lines.push_back(string(buffer));
      i = sem_trywait(s5);
      cout << buffer << endl;
      sem_post(s4);
      // End Critical Section
    }

    wait(nullptr);
    return (0);
  }

  // Child Process
  if (n1 == 0) {
    pthread_t thread1, thread2, thread3, thread4;
    int rc1, rc2, rc3, rc4;

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
    if (num_of_strings == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    continue_loop = sem_open(LOOP_SEM_NAME, O_CREAT, 0660, 1);
    if (continue_loop == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    s3 = sem_open(S3_NAME, O_CREAT, 0660, 0);
    if (s3 == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    s4 = sem_open(S4_NAME, O_CREAT, 0660, 1);
    if (s4 == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    s5 = sem_open(S5_NAME, O_CREAT, 0660, 0);
    if (s5 == SEM_FAILED)
      cout << "Error Creating Semaphore: " << errno << endl;

    shared_mem_ptr = mmap(nullptr, sizeof(buffer), PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd_shm, 0);
    if (shared_mem_ptr == MAP_FAILED)
      cout << "Error Mapping Memory: " << errno << endl;

    // Read all files from shared memory into a vector
    int i;
    while (i != -1) {
      // Critial Section
      sem_wait(write_perm);
      memset(buffer, 0, sizeof(buffer));
      strcpy(buffer, static_cast<char*>(shared_mem_ptr));
      lines.push_back(string(buffer));
      i = sem_trywait(num_of_strings);
      sem_post(continue_loop);
      // End Critical Section
    }

    // Even number inputs process exactly 1/4 of the input
    // Odd number inputs, threads 1-3 will process the input/4 rounded down
    // and thread 4 will process the remainder
    int thread_lines = lines.size() / 4;
    struct thread_args t1_args {lines, matching_lines, 0,
      thread_lines - 1, string(argv[2])};
    struct thread_args t2_args {lines, matching_lines, thread_lines,
      (thread_lines * 2) - 1, string(argv[2])};
    struct thread_args t3_args {lines, matching_lines, thread_lines * 2,
      (thread_lines * 3) - 1, string(argv[2])};
    struct thread_args t4_args {lines, matching_lines, thread_lines * 3,
      lines.size(), string(argv[2])};

    rc1 = pthread_create(&thread1, nullptr, ParseLine,
      static_cast<void*>(&t1_args));
    if (rc1) cout << "Error Creating Thread: " << rc1 << endl;

    rc2 = pthread_create(&thread2, nullptr, ParseLine,
      static_cast<void*>(&t2_args));
    if (rc2) cout << "Error Creating Thread: " << rc2 << endl;

    rc3 = pthread_create(&thread3, nullptr, ParseLine,
      static_cast<void*>(&t3_args));
    if (rc3) cout << "Error Creating Thread: " << rc3 << endl;

    rc4 = pthread_create(&thread4, nullptr, ParseLine,
      static_cast<void*>(&t4_args));
    if (rc4) cout << "Error Creating Thread: " << rc4 << endl;

    // Wait for all threads to finish
    pthread_join(thread1, nullptr);
    pthread_join(thread2, nullptr);
    pthread_join(thread3, nullptr);
    pthread_join(thread4, nullptr);

    for (size_t i = 0; i < matching_lines.size() - 1; ++i) {
      sem_post(s5);
    }
    // Pass all results to parent though shared memory
    for (size_t i = 0; i < matching_lines.size(); ++i) {
      // Critical Section
      sem_wait(s4);
      memset(buffer, 0, sizeof(buffer));
      strcpy(buffer, matching_lines.at(i).c_str());
      memcpy(shared_mem_ptr, buffer, sizeof(buffer));
      sem_post(s3);
      // End Critical Section
    }
    return (0);
  }
}
