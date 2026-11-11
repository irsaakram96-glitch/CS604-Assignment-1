#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>

#define ID_SIZE 100
#define REPORT_SIZE 1024
#define TASK_COUNT 3

typedef struct {
    long long total_time;
    int longest_task;
    int maximum_time;
} TaskResult;

int write_all(int fd, const void *buffer, size_t size) {
    const char *ptr = (const char *)buffer;
    size_t written = 0;

    while (written < size) {
        ssize_t n = write(fd, ptr + written, size - written);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        written += (size_t)n;
    }

    return 0;
}

int read_all(int fd, void *buffer, size_t size) {
    char *ptr = (char *)buffer;
    size_t received = 0;

    while (received < size) {
        ssize_t n = read(fd, ptr + received, size - received);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        received += (size_t)n;
    }

    return 0;
}

int main(void) {
    char student_id[ID_SIZE];
    char filename[ID_SIZE + 20];
    char report[REPORT_SIZE];

    int task_times[TASK_COUNT];
    int parent_to_child[2];
    int child_to_parent[2];

    pid_t pid;
    TaskResult result;

    int fd;
    int status;

    printf("Enter Student ID: ");

    if (scanf("%99s", student_id) != 1) {
        fprintf(stderr, "Error: Invalid Student ID.\n");
        return EXIT_FAILURE;
    }

    for (int i = 0; student_id[i] != '\0'; i++) {
        char ch = student_id[i];

        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-')) {
            fprintf(stderr, "Error: Invalid Student ID format.\n");
            return EXIT_FAILURE;
        }
    }

    for (int i = 0; i < TASK_COUNT; i++) {
        printf("Enter Task %d Execution Time: ", i + 1);

        if (scanf("%d", &task_times[i]) != 1 ||
            task_times[i] <= 0) {
            fprintf(stderr,
                    "Error: Task execution time must be a positive integer.\n");
            return EXIT_FAILURE;
        }
    }

    if (pipe(parent_to_child) == -1) {
        perror("Error creating first pipe");
        return EXIT_FAILURE;
    }

    if (pipe(child_to_parent) == -1) {
        perror("Error creating second pipe");

        close(parent_to_child[0]);
        close(parent_to_child[1]);

        return EXIT_FAILURE;
    }

    pid = fork();

    if (pid < 0) {
        perror("Error creating child process");

        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);

        return EXIT_FAILURE;
    }

    if (pid == 0) {
        int child_times[TASK_COUNT];
        TaskResult child_result;

        close(parent_to_child[1]);
        close(child_to_parent[0]);

        if (read_all(parent_to_child[0],
                     child_times,
                     sizeof(child_times)) == -1) {
            perror("Child: Error reading task times");

            close(parent_to_child[0]);
            close(child_to_parent[1]);

            _exit(EXIT_FAILURE);
        }

        close(parent_to_child[0]);

        child_result.total_time = 0;
        child_result.longest_task = 1;
        child_result.maximum_time = child_times[0];

        for (int i = 0; i < TASK_COUNT; i++) {
            child_result.total_time += child_times[i];

            if (child_times[i] > child_result.maximum_time) {
                child_result.maximum_time = child_times[i];
                child_result.longest_task = i + 1;
            }
        }

        if (write_all(child_to_parent[1],
                      &child_result,
                      sizeof(child_result)) == -1) {
            perror("Child: Error sending results");

            close(child_to_parent[1]);
            _exit(EXIT_FAILURE);
        }

        close(child_to_parent[1]);

        _exit(EXIT_SUCCESS);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);

    if (write_all(parent_to_child[1],
                  task_times,
                  sizeof(task_times)) == -1) {
        perror("Parent: Error sending task times");

        close(parent_to_child[1]);
        close(child_to_parent[0]);

        waitpid(pid, NULL, 0);

        return EXIT_FAILURE;
    }

    close(parent_to_child[1]);

    if (read_all(child_to_parent[0],
                 &result,
                 sizeof(result)) == -1) {
        fprintf(stderr, "Parent: Error receiving child results.\n");

        close(child_to_parent[0]);

        waitpid(pid, NULL, 0);

        return EXIT_FAILURE;
    }

    close(child_to_parent[0]);

    if (waitpid(pid, &status, 0) == -1) {
        perror("Error waiting for child");
        return EXIT_FAILURE;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Error: Child process failed.\n");
        return EXIT_FAILURE;
    }

    snprintf(filename,
             sizeof(filename),
             "%s_report.txt",
             student_id);

    int length = snprintf(
        report,
        sizeof(report),
        "\n--- Task Processing Report ---\n"
        "Student ID: %s\n"
        "Task 1: %d units\n"
        "Task 2: %d units\n"
        "Task 3: %d units\n"
        "Total Execution Time: %lld units\n"
        "Longest Task: Task %d\n"
        "Maximum Execution Time: %d units\n"
        "Report saved to: %s\n",
        student_id,
        task_times[0],
        task_times[1],
        task_times[2],
        result.total_time,
        result.longest_task,
        result.maximum_time,
        filename
    );

    if (length < 0 || (size_t)length >= sizeof(report)) {
        fprintf(stderr, "Error: Report buffer is too small.\n");
        return EXIT_FAILURE;
    }

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd == -1) {
        perror("Error opening report file");
        return EXIT_FAILURE;
    }

    if (write_all(fd, report, (size_t)length) == -1) {
        perror("Error writing report file");

        close(fd);

        return EXIT_FAILURE;
    }

    if (close(fd) == -1) {
        perror("Error closing report file");
        return EXIT_FAILURE;
    }

    printf("%s", report);

    return EXIT_SUCCESS;
}
