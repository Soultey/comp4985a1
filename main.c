// to run your program
// cd into "build" directory
// run the command ./main

// everytime you make changes to your files
// go to terminal
// cd into project directory "comp4985a1"
// run command "./build.sh"

// to fix main(65271,0x1dd805000) malloc: nano zone abandoned due to inability to reserve vm space. error
// open terminal
// run "export MallocNanoZone=0"

#include <stdio.h>

int main(void)
{
    printf("Hello");
    return 0;
}
