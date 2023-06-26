#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 4){
    printf(2, "Usage: ln old new\n");
    exit();
  }
  if (strcmp(argv[1], "-h") == 0) {
    printf(2, "hard link call\n");
    if(link(argv[2], argv[3]) < 0)
      printf(2, "link %s %s: failed\n", argv[1], argv[2]);
    exit();
  } else if (strcmp(argv[1], "-s") == 0) {
    printf(2, "symbolic link call\n");
    symlink(argv[2], argv[3]);
    exit();
  }
  exit();
}
