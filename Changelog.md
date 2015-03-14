MASSIVE UPDATE! WELCOME TO 2011!

Well, I started around December 15th, and my goal was to have a brand new FATX library (libfatx) finished by the new year. I came pretty close, but I didn't quite get to write support and file creation/truncation/deletion.

Anyways, here's the shape of things to come: x360 is no longer a FUSE filesystem driver for the Xbox 360 hard drive, but is now a collection of tools (one of which is the filesystem driver) pertaining to all things Xbox. It's the only tool at this time, but I really want to add more things (a command-line gamesave resigner, or similar).

I've been away from this project for too long, so my resolution is to spend more time on it :)

But enough of this nonsensical rambling! Here are the new features:

  * Support for little-endian filesystems as well as big-endian ones. Also, I'm using macros such as be32toh, so if there's anyone out there who wants to try this on an Xbox 360 running linux (I know there was a comment of the likes before), now would be a great time! (But, I give no guarantees.)

  * Support for filesystems with 16-bit FATs, as well as 32-bit FATs. Speaking of which...

  * Support for partitions 0, 2, and 3 (by their Xplorer360 given names). Although, I gotta warn you, I couldn't read my partition 0 with this or in Xplorer360. I think MS started encrypting it.

  * Support for timestamps! FATX to unix and vice versa. I know it's a moot point (who cares about timestamps), but I personally was so excited once I discovered the trick. That time issue was bothering me on such a big level.

  * Multi-cluster directories are now supported.

  * Loads more error checking.

This new driver is a complete rewrite, however, and there are some things I just haven't gotten around to yet:

  * Writing is not supported
  * Creating files is not supported
  * Renaming is not supported
  * Resizing (truncating) is not supported
  * Deleting is not supported
  * Arbitrary uid/gid is not supported (who cares)

Although it looks like we've lost a lot of features, I'm confident that this new version is a step forward (especially with its level of error checking). Plus, if your disk is /dev/sdb, or example, the partitions show up as /dev/sdb1, /dev/sdb2, and /dev/sdb3. How can you beat that? (See the readme for more information on the partition helper).

Well, I could go on forever, but I won't. If there's anything that isn't covered here or in the readme, drop a comment. Oh, and if you want the old version, look in branches/retro.

## Old Revisions (now in branches/retro) ##

Revisions newer than [r15](https://code.google.com/p/x360/source/detail?r=15) have README files with this information.

### [r15](https://code.google.com/p/x360/source/detail?r=15) ###
|Features:|Implementation:|Comments:|
|:--------|:--------------|:--------|
|Directory Listing:|80%|multi-cluster directories are not supported|
|File Stating:|60%|name, file vs directory, and file size are supported; time is weirdly supported (see known issues)|
|File Creation:|100%|Creates an entry in the directory table and allocates a cluster for use|
|File Reading:|100%|full file read support with error-checking|
|File Writing:|100%|full file write support with error-checking|
|File Renaming:|50%|Renaming across directories (moving) is not supported.|
|File Truncation:|100%|Truncating allocates/frees clusters appropriately|
|File Deletion:|100%|Marks directory entry as 'deleted' and frees clusters|
|Partitions:|5%|partition 3 support only; automatic size detection|
|Multi-User Support:|60%|mounted files have user's uid/gid as owner uid/gid; user can set arbitrary uid/gid for owner|

Options:
|Option:|Effect:|
|:------|:------|
|-d|Debug Mode|
|-g|Set group GID (decimal)|
|-h|Usage Message (also no arguements)|
|-o|Offset-Beginning of partition (decimal)|
|-u|Set owner UID (decimal)|

Known Issues:
  * Only 32-bit filesystems are supported (16-bit will need to be supported as well once multiple partitions are supported)
  * '-o' only takes offsets in decimal notation.
  * Time problem still exists (and probably will for a long time)

Comments:
  * I was using lseek followed by read to read directories and files; then I realized that FUSE is multi-threaded. Switched to pread/pwrite.
  * Learned about mmap; consequently the fat is now mmap'ed.
  * This driver is now at a point where I would feel safe using it to write to my hard drive.

### [r13](https://code.google.com/p/x360/source/detail?r=13) ###
|Features:|Implementation:|Comments:|
|:--------|:--------------|:--------|
|Directory Listing:|80%|multi-cluster directories are not supported|
|File Stating:|60%|name, file vs directory, and file size are supported; time is weirdly supported (see known issues)|
|File Creation:|100%|Creates an entry in the directory table and allocates a cluster for use|
|File Reading:|100%|full file read support with error-checking|
|File Writing:|80%|grows file with 'truncate()' then writes data in-place|
|File Renaming:|50%|Renaming across directories (moving) is not supported.|
|File Truncation:|40%|Truncating up cannot allocate new clusters (instead returns ENOSPC); Truncating down does not free clusters|
|File Deletion:|100%|Marks directory entry as 'deleted' and frees clusters (have not tested on multi-cluster file)|
|Partitions:|5%|partition 3 support only; automatic size detection|
|Multi-User Support:|60%|mounted files have user's uid/gid as owner uid/gid; user can set arbitrary uid/gid for owner|

Options:
|Option:|Effect:|
|:------|:------|
|-d|Debug Mode|
|-g|Set group GID (decimal)|
|-h|Usage Message (also no arguements)|
|-o|Offset-Beginning of partition (decimal)|
|-u|Set owner UID (decimal)|

Known Issues:
  * Only 32-bit filesystems are supported (16-bit will need to be supported as well once multiple partitions are supported)
  * '-o' only takes offsets in decimal notation.
  * Time problem still exists (and probably will for a long time)

Comments:
  * Write support is theoretically full, but since truncate is crippled, I marked it at 80%.
  * All functions required to use 'gedit' are now (partially) implemented. Go ahead and try it on a dummy fs image. It's fun!
  * Once 'truncate()' and 'readdir()' are fixed, we will have reached Xplorer360-like functionality (except for partitions, but nobody cares about partitions 1 and 2 anyways)

### [r10](https://code.google.com/p/x360/source/detail?r=10) ###
|Features:|Implementation:|Comments:|
|:--------|:--------------|:--------|
|Directory Listing:|80%|multi-cluster directories are not supported|
|File Stating:|60%|name, file vs directory, and file size are supported; time is weirdly supported (see known issues)|
|File Reading:|100%|full file read support with error-checking (I think)|
|Partitions:|5%|partition 3 support only; automatic size detection|
|Multi-User Support:|60%|mounted files have user's uid/gid as owner uid/gid; user can set arbitrary uid/gid for owner|

Options:
|Option:|Effect:|
|:------|:------|
|-d|Debug Mode|
|-g|Set group GID (decimal)|
|-h|Usage Message (also no arguements)|
|-o|Offset-Beginning of partition (decimal)|
|-u|Set owner UID (decimal)|

Known Issues:
  * Only 32-bit filesystems are supported (16-bit will need to be supported as well once multiple partitions are supported)
  * '-o' only takes offsets in decimal notation.
  * Time is theoretically supported (which means it's fully supported, but I can't figure out why the last access date is 2048 :) At least it won't all be 1970 anymore :)

Comments:
  * You are no longer forced to use debug mode (yay!) To enable debug mode, use the option '-d' on the command line.
  * FATX images can be mounted. If you pass a normal file to be mounted, the partition offset is set to zero (so that the image can be of the partition only, instead of the entire drive)

### [r7](https://code.google.com/p/x360/source/detail?r=7) ###
|Features:|Implementation:|Comments:|
|:--------|:--------------|:--------|
|Directory Listing:|80%|multi-cluster directories are not supported|
|File Stating:|50%|name, file vs directory, and file size are supported; time is not supported|
|File Reading:|95%|full file read support; missing some error-checking so it might fail on a corrupted filesystem|
|Partitions:|5%|partition 3 support only; automatic size detection|

Known Issues:
  * Only 32-bit filesystems are supported (16-bit will need to be supported as well once multiple partitions are supported)

Comments:
  * Replaced multiplication & division with bit shifts (it seemed as if I was only ever multiplying or dividing by 0x1000 and 0x4000; shifts would be way faster)

### [r2](https://code.google.com/p/x360/source/detail?r=2) ###
|Features:|Implementation:|Comments:|
|:--------|:--------------|:--------|
|Directory Listing:|80%|multi-cluster directories are not supported|
|File Stating:|50%|name, file vs directory, and file size are supported; time is not supported|
|File Reading:|95%|full file read support; missing some error-checking so it might fail on a corrupted filesystem|
|Partitions:|3%|multiple partitions are not supported; plan for this in the future|

Known Issues:
  * Offsets are hard-coded for the 120GB hard drive (I'm about 50% done with hard drive size detection)
  * Only 32-bit filesystems are supported (16-bit will need to be supported as well once multiple partitions are supported)
  * I accidentally took out the variable 'slash', as I thought I was no longer using it. My bad. Just add `static const char *slash = "/";` somewhere at the top. Fixed in [r7](https://code.google.com/p/x360/source/detail?r=7).