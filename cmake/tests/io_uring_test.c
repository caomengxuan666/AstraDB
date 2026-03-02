// Test if io_uring is available on the system
#define ASIO_HAS_IO_URING 1
#include <linux/io_uring.h>
#include <unistd.h>

int main() {
    struct io_uring_params params;
    int fd = io_uring_setup(0, &params);
    
    if (fd < 0) {
        return 1;  // io_uring not available
    }
    
    close(fd);
    return 0;  // io_uring is available
}