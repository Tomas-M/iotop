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

[![Packaging status](https://repology.org/badge/tiny-repos/iotop-c.svg)](https://repology.org/project/iotop-c/versions)

## Sample

![Sample Image](https://github.com/EinProfispieler/iotop/blob/master/.sample/demo.png)

## How to make
Require root access, be noticed in case prompt errors.

<details>
  <summary>Ubuntu</summary>
    
    apt install build-essential ncurses-dev -y
    git clone https://github.com/Tomas-M/iotop
    cd iotop
    make
</details>

<details>
  <summary>CentOS 7</summary>
    CentOS did not pre-install git, manually install might be needed. also install 'epel-release' Package is recommended.
    
    yum install ncurses-devel pkgconfig -y
    git clone https://github.com/Thomas-M/iotop
    cd iotop
    make
</details>

<details>
  <summary>Arm64 Linux</summary>
  For Arm64 Linux System, eg. Raspberry PI 4. <b>Tested on Ubuntu 20.04 Arm64</b>.
 
    apt install build-essential ncurses-dev -y
    git clone https://github.com/Tomas-M/iotop
    cd iotop
    make -f Makearm64
</detail>

## Make it work as a command
sudo mv iotop /usr/sbin

## How to update to latest version

cd iotop
git checkout master
git pull
make


## Options


```
-v, --version         show program's version number and exit
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
-1, --hide-pid        hide PID/TID column
-2, --hide-prio       hide PRIO column
-3, --hide-user       hide USER column
-4, --hide-read       hide DISK READ column
-5, --hide-write      hide DISK WRITE column
-6, --hide-swapin     hide SWAPIN column
-7, --hide-io         hide IO column
-8, --hide-graph      hide GRAPH column
-9, --hide-command    hide COMMAND column
-q, --quiet           suppress some lines of header (implies --batch)
-H, --no-help         suppress listing of shortcuts
```

Contribute
==========

iotop was originally written by Vyacheslav Trushkin in 2014, distributed by Tomas Matejicek and later improved by Boian Bonev.

iotop is maintaned on GitHub at https://github.com/Tomas-M/iotop

The preferred way to contribute to the project is to file a pull request at GitHub.

Contacts of current maintainers are:

- Tomas Matejicek <tomas@slax.org>
- Boian Bonev <bbonev@ipacct.com>

Notable contributions (ordered by time of last contribution):

- Paul Wise <pabs@debian.org> - Debian packaging, man page, multiple reviews and ideas
- Rumen Jekov <rvjekov@gmail.com> - Arch Linux packaging and testing
- Arthur Zamarin <arthurzam+gentoo@gmail.com> - Gentoo packaging and testing
- Yuriy M. Kaminskiy <yumkam@gmail.com> - code fixes and improvements
- alicektx <alicekot13@gmail.com> - documentation imrpovements
- Filip Kofron <filip.kofron.cz@gmail.com> - build system imrpovements

**Thanks! This project is what it is now because the steam you have put into it**

*NB. In case you have contributed to the project and do not see your name in the list, please note that the above list is updated manually and it is an omission - notify the maintainers to fix it.*
