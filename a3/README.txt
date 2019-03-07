Runhao Liang, liangru3
Csc209_a3

This paragraph will discuss why it is difficult to differentiate hard links and regular files. Underneath the file system, files are represented by inodes, which store information. When a hard link is created, it is linked to the same inode of the original file. Then the new hard link and the original file share the same file-i.d. and ohter data. Moreover, deleting and moving the original file cannot affect the connection between the hard link and the inode. As a result, a hard link is the "same" original file for the OS processes. 