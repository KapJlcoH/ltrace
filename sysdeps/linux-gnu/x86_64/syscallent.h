	"read",                            /* 0 */
	"write",                           /* 1 */
	"open",                            /* 2 */
	"close",                           /* 3 */
	"stat",                            /* 4 */
	"fstat",                           /* 5 */
	"lstat",                           /* 6 */
	"poll",                            /* 7 */
	"lseek",                           /* 8 */
	"mmap",                            /* 9 */
	"mprotect",                        /* 10 */
	"munmap",                          /* 11 */
	"brk",                             /* 12 */
	"rt_sigaction",                    /* 13 */
	"rt_sigprocmask",                  /* 14 */
	"rt_sigreturn",                    /* 15 */
	"ioctl",                           /* 16 */
	"pread",                           /* 17 */
	"pwrite",                          /* 18 */
	"readv",                           /* 19 */
	"writev",                          /* 20 */
	"access",                          /* 21 */
	"pipe",                            /* 22 */
	"select",                          /* 23 */
	"sched_yield",                     /* 24 */
	"mremap",                          /* 25 */
	"msync",                           /* 26 */
	"mincore",                         /* 27 */
	"madvise",                         /* 28 */
	"shmget",                          /* 29 */
	"shmat",                           /* 30 */
	"shmctl",                          /* 31 */
	"dup",                             /* 32 */
	"dup2",                            /* 33 */
	"pause",                           /* 34 */
	"nanosleep",                       /* 35 */
	"getitimer",                       /* 36 */
	"alarm",                           /* 37 */
	"setitimer",                       /* 38 */
	"getpid",                          /* 39 */
	"sendfile",                        /* 40 */
	"socket",                          /* 41 */
	"connect",                         /* 42 */
	"accept",                          /* 43 */
	"sendto",                          /* 44 */
	"recvfrom",                        /* 45 */
	"sendmsg",                         /* 46 */
	"recvmsg",                         /* 47 */
	"shutdown",                        /* 48 */
	"bind",                            /* 49 */
	"listen",                          /* 50 */
	"getsockname",                     /* 51 */
	"getpeername",                     /* 52 */
	"socketpair",                      /* 53 */
	"setsockopt",                      /* 54 */
	"getsockopt",                      /* 55 */
	"clone",                           /* 56 */
	"fork",                            /* 57 */
	"vfork",                           /* 58 */
	"execve",                          /* 59 */
	"exit",                            /* 60 */
	"wait4",                           /* 61 */
	"kill",                            /* 62 */
	"uname",                           /* 63 */
	"semget",                          /* 64 */
	"semop",                           /* 65 */
	"semctl",                          /* 66 */
	"shmdt",                           /* 67 */
	"msgget",                          /* 68 */
	"msgsnd",                          /* 69 */
	"msgrcv",                          /* 70 */
	"msgctl",                          /* 71 */
	"fcntl",                           /* 72 */
	"flock",                           /* 73 */
	"fsync",                           /* 74 */
	"fdatasync",                       /* 75 */
	"truncate",                        /* 76 */
	"ftruncate",                       /* 77 */
	"getdents",                        /* 78 */
	"getcwd",                          /* 79 */
	"chdir",                           /* 80 */
	"fchdir",                          /* 81 */
	"rename",                          /* 82 */
	"mkdir",                           /* 83 */
	"rmdir",                           /* 84 */
	"creat",                           /* 85 */
	"link",                            /* 86 */
	"unlink",                          /* 87 */
	"symlink",                         /* 88 */
	"readlink",                        /* 89 */
	"chmod",                           /* 90 */
	"fchmod",                          /* 91 */
	"chown",                           /* 92 */
	"fchown",                          /* 93 */
	"lchown",                          /* 94 */
	"umask",                           /* 95 */
	"gettimeofday",                    /* 96 */
	"getrlimit",                       /* 97 */
	"getrusage",                       /* 98 */
	"sysinfo",                         /* 99 */
	"times",                           /* 100 */
	"ptrace",                          /* 101 */
	"getuid",                          /* 102 */
	"syslog",                          /* 103 */
	"getgid",                          /* 104 */
	"setuid",                          /* 105 */
	"setgid",                          /* 106 */
	"geteuid",                         /* 107 */
	"getegid",                         /* 108 */
	"setpgid",                         /* 109 */
	"getppid",                         /* 110 */
	"getpgrp",                         /* 111 */
	"setsid",                          /* 112 */
	"setreuid",                        /* 113 */
	"setregid",                        /* 114 */
	"getgroups",                       /* 115 */
	"setgroups",                       /* 116 */
	"setresuid",                       /* 117 */
	"getresuid",                       /* 118 */
	"setresgid",                       /* 119 */
	"getresgid",                       /* 120 */
	"getpgid",                         /* 121 */
	"setfsuid",                        /* 122 */
	"setfsgid",                        /* 123 */
	"getsid",                          /* 124 */
	"capget",                          /* 125 */
	"capset",                          /* 126 */
	"rt_sigpending",                   /* 127 */
	"rt_sigtimedwait",                 /* 128 */
	"rt_sigqueueinfo",                 /* 129 */
	"rt_sigsuspend",                   /* 130 */
	"sigaltstack",                     /* 131 */
	"utime",                           /* 132 */
	"mknod",                           /* 133 */
	"uselib",                          /* 134 */
	"personality",                     /* 135 */
	"ustat",                           /* 136 */
	"statfs",                          /* 137 */
	"fstatfs",                         /* 138 */
	"sysfs",                           /* 139 */
	"getpriority",                     /* 140 */
	"setpriority",                     /* 141 */
	"sched_setparam",                  /* 142 */
	"sched_getparam",                  /* 143 */
	"sched_setscheduler",              /* 144 */
	"sched_getscheduler",              /* 145 */
	"sched_get_priority_max",          /* 146 */
	"sched_get_priority_min",          /* 147 */
	"sched_rr_get_interval",           /* 148 */
	"mlock",                           /* 149 */
	"munlock",                         /* 150 */
	"mlockall",                        /* 151 */
	"munlockall",                      /* 152 */
	"vhangup",                         /* 153 */
	"modify_ldt",                      /* 154 */
	"pivot_root",                      /* 155 */
	"_sysctl",                         /* 156 */
	"prctl",                           /* 157 */
	"arch_prctl",                      /* 158 */
	"adjtimex",                        /* 159 */
	"setrlimit",                       /* 160 */
	"chroot",                          /* 161 */
	"sync",                            /* 162 */
	"acct",                            /* 163 */
	"settimeofday",                    /* 164 */
	"mount",                           /* 165 */
	"umount2",                         /* 166 */
	"swapon",                          /* 167 */
	"swapoff",                         /* 168 */
	"reboot",                          /* 169 */
	"sethostname",                     /* 170 */
	"setdomainname",                   /* 171 */
	"iopl",                            /* 172 */
	"ioperm",                          /* 173 */
	"create_module",                   /* 174 */
	"init_module",                     /* 175 */
	"delete_module",                   /* 176 */
	"get_kernel_syms",                 /* 177 */
	"query_module",                    /* 178 */
	"quotactl",                        /* 179 */
	"nfsservctl",                      /* 180 */
	"getpmsg",                         /* 181 */
	"putpmsg",                         /* 182 */
	"afs_syscall",                     /* 183 */
	"tuxcall",                         /* 184 */
	"security",                        /* 185 */
	"gettid",                          /* 186 */
	"readahead",                       /* 187 */
	"setxattr",                        /* 188 */
	"lsetxattr",                       /* 189 */
	"fsetxattr",                       /* 190 */
	"getxattr",                        /* 191 */
	"lgetxattr",                       /* 192 */
	"fgetxattr",                       /* 193 */
	"listxattr",                       /* 194 */
	"llistxattr",                      /* 195 */
	"flistxattr",                      /* 196 */
	"removexattr",                     /* 197 */
	"lremovexattr",                    /* 198 */
	"fremovexattr",                    /* 199 */
	"tkill",                           /* 200 */
	"time",                            /* 201 */
	"futex",                           /* 202 */
