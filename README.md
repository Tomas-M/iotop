iotop
=====

Your Linux server is too slow or load is too high? One of the possible
causes of such symptoms may be high IO (input/output) waiting time,
which basically means that some of your processes need to read or write
to a hard drive while it is too slow and not ready yet, serving data for
some other processes.

Common practice is to use iostat -x in order to find out which block
device (hard drive) is slow, but such information is not always much
helpful. It could help you much more if you knew which process reads or
writes the most data from your slow disk, so you could possibly renice
it using ionice or even kill it.

IOTop will identify processes, which use high amount of input/output
requests on your machine. It is similar to the well known top utility,
but instead of showing you what consumes CPU the most, it lists
processes by their IO consumption. Inspired by iotop python script from
Guillaume Chazarain, but rewritten to C by Vyacheslav Trushkin so it now
runs without python at all.


How to make
===========

    git clone
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
-q, --quiet           suppress some lines of header (implies --batch)
```
