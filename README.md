iotop
=====

Is your Linux server too slow or load is too high? One of the possible
causes of such symptoms may be high IO (input/output) waiting time,
which basically means that some of your processes need to read or write
to a hard drive while it is too slow and not ready yet, serving data for
some other processes.

Common practice is to use iostat -x in order to find out which block
device (hard drive) is slow, but this information is not always helpful.
It could help you much more if you knew which process reads or writes
the most data from your slow disk, so you could renice it using ionice
or even kill it.

iotop identifies processes that use high amount of input/output requests
on your machine. It is similar to the well known top utility, but
instead of showing you what consumes CPU the most, it lists
processes by their IO usage. Inspired by iotop Python script from
Guillaume Chazarain, rewritten in C by Vyacheslav Trushkin and improved
by Boian Bonev so it runs without Python at all.

iotop is licensed GPL-2.0+


How to make
===========

    # apt-install build-essential ncurses-dev
    git clone https://github.com/Tomas-M/iotop
    cd iotop
    make


Options
=======

```
--version             show program's version number and exit
-h, --help            show this help message and exit
-o, --only            only show processes or threads actually doing I/O
-b, --batch           non-interactive mode
-n NUM, --iter=NUM    number of iterations before ending [infinite]
-d SEC, --delay=SEC   delay between iterations [1 second]
-p PID, --pid=PID     processes/threads to monitor [all]
-u USER, --user=USER  users to monitor [all]
-P, --processes       only show processes, not all threads
-a, --accumulated     show accumulated I/O instead of bandwidth
-k, --kilobytes       use kilobytes instead of a human friendly unit
-t, --time            add a timestamp on each line (implies --batch)
-c, --fullcmdline     show full command line
-q, --quiet           suppress some lines of header (implies --batch)
--no-help             suppress listing of shortcuts
```

Contribute
==========

iotop was originally written by Vyacheslav Trushkin in 2014, distributed by Tomas Matejicek and later improved by Boian Bonev.

iotop is maintaned on GitHub at https://github.com/Tomas-M/iotop

The preferred way to contribute to the project is to file a pull request at GitHub.

Contacts of current maintainers are:

- Tomas Matejicek <tomas@slax.org>
- Boian Bonev <bbonev@ipacct.com>
