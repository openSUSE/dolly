DOLLY
=====
A program to clone over the network disks / partitions / files / directories.
Take same amount of time to copy data to one node or to X nodes.

SYNOPSIS
========

General syntax for files/devices:
````sh
dolly -v -a timeout -r max_retries -s -I infile -O outfile|- -H hostlist
````

General syntax for directories:
````sh
dolly -v -a timeout -r max_retries -s -D DIR -O TARGET_DIR -H hostlist
````

Dolly as a server using a configuration file:
````sh
dolly -s -f config_file
````

Dolly as a server, verbose mode, trying to **reconnect 40 times**, **copy /dev/vdd to /dev/vdd** on nodes **IPNODE1,IPNODE2,IPNODE3**:
````sh
dolly -v -r 40 -S SERVERIP -H IPNODE1,IPNODE2,IPNODE3 -I /dev/vdd -O /dev/vdd
````

Dolly as a server, using **systemd socket** to connect to nodes, **copy /dev/vdd to /dev/vdd** on nodes **IPNODE1,IPNODE2,IPNODE3**:
````sh
dolly -d -H IPNODE1,IPNODE2,IPNODE3 -I /dev/vdd
````

Dolly as a client in verbose mode:
````sh
dolly -v
````

Dolly server copy directory **/data/llm to /** on **IPNODE1,IPNODE2,IPNODE3**, **excluding dir /data/llm/mixtral**:
````sh
dolly -s -v -H IPNODE1,IPNODE2,IPNODE3 -D /data/llm -X /data/llm/mixtral -O /
````


DESCRIPTION
===========

Dolly is used to clone the installation of one machine to (possibly
many) other machines. It can distribute image-files,
partitions or whole hard disk drives to other partitions or hard disk
drives. As it forms a "virtual TCP ring" to distribute data, it works
best with fast switched networks.


OPTIONS
=======
If used without a configuration file following three commandline options must be
set:

**-I** FILE : FILE is used as input file.

**-O** FILE\|- : FILE will be used as output file, if '-' is used as FILE, the
output will printed to stdout. This is not mandatory on the commande line, if 
not specified this will use the same value as the one set in **-I** option.

**-D** DIR,DIR2 : Directories to copy to clients

**-X** DIR,DIR2 : Directories to exclude (not copy to clients)

**-P** <password>
:   Sets a password to secure the dolly ring. All nodes (server and all clients) must use the exact same password to connect. This prevents unauthorized clients from joining the ring and unauthorized servers from sending data or commands.

    -   **Client Behavior:** A client started with `-P` will only accept a connection from a server (or parent client) that provides the correct password. Connection attempts with an incorrect password will be rejected, and the client will continue to wait for a valid connection.

    -   **Server Behavior:** A server started with `-P` requires all its clients to authenticate successfully. If any client fails the password check during the ring setup, the server will abort.

    -   **Interaction with -K:** To terminate a password-protected client, the `-K` command must be used with the corresponding `-P` option.

**-H** <host1,host2,...>
:   Specifies a comma-separated list of client hostnames or IP addresses that will form the data distribution chain. When used, `dolly` automatically operates in server mode.

    The server will connect to the first host in this list. Each subsequent host in the list will connect to the next host, forming a daisy chain for efficient data distribution. Data flows from the server to the first client, then from the first client to the second, and so on, until it reaches the last client in the list.

    Hostnames are resolved to IP addresses; this behavior can be controlled by the `-R` (IPv4) or `-6` (IPv6) options.

Following other options are:

  **-h**
 :   Prints a short help and exits.

  **-V**
 :   Prints the version number as well as the date of that version and exits.

  **-v**
 :  This switches to verbose mode in which dolly prints out a little
    bit more information. This option is recommended if you want to
    know what's going on during cloning and it might be helpful during
    debugging.

  **-s**
 :  This option specifies the server machine and should only be used
    on the master. Dolly will warn you if the config file specifies
    another master than the machine on which this option is set.
    This is no more mandatory if server options are used: -H, -I.
    This option must be secified before the "-f" option!

  **-K** <hostname[,hostname2,...]>
 :  Remotely terminates `dolly` client processes that are waiting for a
    server connection. This is useful for cleaning up stale or duplicate
    `dolly` processes on client machines to ensure a clean start for a new
    cloning ring.

    The command accepts a single hostname or a comma-separated list of hostnames.

    **Important:** If a client was started with a password (using the `-P` option), the `-K` command **must** also be invoked with the same `-P <password>` option. An attempt to kill a password-protected client without the correct password will be rejected by the client, and the process will not be terminated.

    Example (single client):
    ```sh
    dolly -K node01 -P mysecretpassword
    ```

    Example (multiple clients):
    ```sh
    dolly -K node01,node02 -P mysecretpassword
    ```

  **-S**
 :  Same as "-s", but dolly will not warn you if the server's hostname
    and the name specified in the config file do not match.

 **-R**
 :  resolve the hostnames to ipv4 addresses

 **-6**
 :  resolve the hostnames to ipv6 addresses

  **-q**
 :  Usually dolly will print a warning when the select() system call
    is interrupted by a signal. This option suppresses these warnings.

  **-d**
 :  Connect to systemd socket on clients nodes to start the dolly 
    service (port 9996). This option only available on command 
    line.
    **Warning !!**
    By default if you use the dolly.socket, the dolly.service start
    as root user, which means that you can delete all your nodes
    data easily while pushing into the ring data in the wrong place.
    Use **-P** option on client to restrict data from a specific
    server.

  **-f** <config file>
 :  This option is used to select the config file for this cloning
    process. This option makes only sense on the master machine and
    the configuration file must exist on the master.

  **-o** <logfile>
 :  This option specifies the logfile. Dolly will write some
    statistical information into the logfile. it is mostly
    used when benchmarking switches. The format of the lines in the
    logfile is as follows:
    Trans. data  Segsize Clients Time      Dataflow  Agg. dataflow
    [MB]         [Byte]  [#]     [s]       [MB/s]    [MB/s]

  **-a** <timeout>
 :  Sometimes it might be useful if Dolly would terminate instead of
    waiting indefinitely in case something goes wrong. This option
    lets you specify this timeout. If dolly could not transfer any
    data after <timeout> seconds, then it will simply print an error
    message and terminate. This feature might be especially useful for
    scripted and automatic installations where you don't want to have
    dolly-processes hang around if a machine hangs.

  **-n**
 :  Do not sync() before exit. Thus, dolly will exit sooner, but data
    may not make it to disk if power fails soon after dolly exits.

 **-r** <n>
 :  Retry to connect to node <n> times


CONFIGURATION FILE
==================

As an alternative to providing the hostlist and file paths on the command line, `dolly` can be configured using a configuration file. This file defines the data source, the destination path on the clients, and the list of clients that form the distribution chain.

The server reads this file when started with the `-f <config_file>` option.

### Example `dollytab` file:
```
# This is a comment. Lines starting with '#' are ignored.
infile /dev/sda5
outfile /dev/sda5

# Optional settings
hyphennormal

# List of clients in the chain
node1-giga
node2-giga
node3-giga

# End of configuration
endconfig
```

### Directives

The configuration file is parsed line-by-line and generally expects directives in a specific order.

3.  **`infile`** (Required)
    :   Specifies the source file or device on the server machine.
    *   **Syntax:** `[compressed] infile <path> [split]`
    *   **Example:** `infile /dev/sda10`
    *   **Experimental Options:**
        *   `compressed`: Indicates the input is compressed.
        *   `split`: Instructs Dolly to read from multiple input files named `<path>_<number>`.

4.  **`outfile`** (Required)
    :   Specifies the destination file or device on all client machines.
    *   **Syntax:** `outfile <path> [split <n>(k|M|G|T)]`
    *   **Example:** `outfile /dev/sda10`
    *   **Experimental Options:**
        *   `split <size>`: Instructs clients to split the output into chunks of the specified size (e.g., `split 2G`).

5.  **`hyphennormal`** (Optional)
    :   The optional keyword "hypennormal" instructs Dolly to treat the '-' character in hostnames as any other character. By default the hyphen is used to separate the base hostnames from the names of the different interface (e.g. "node12-giga"). You might use this paramater if your hostnames include a hypen (like e.g. "node-12").
    *   **Syntax:** `hyphennormal`

6.  **Client Hostnames** (Required)
    :   A list of client hostnames, one per line, that defines the daisy chain. The server connects to the first client, which connects to the second, and so on.
    *   **Syntax:** `<hostname>`
    *   **Example:**
        ```
        cluster-1-giga
        cluster-2-giga
        ```

7.  **`endconfig`** (Required)
    :   Marks the end of the configuration file. Any lines after this directive are ignored.
    *   **Syntax:** `endconfig`


NOTE on NODES HOSTNAMES
=======================

On some machines (e.g. with very small maintenance installations),
gethostbyname() does not return the hostname (I don't know why). If
you have that problem, you should make sure that the environment
variables MYNODENAME or HOST are set accordingly. Dolly first tries to
get the environment variable MYNODENAME, then HOST, then it tries
gethostbyname(). This feature was introduced in dolly version 0.58.


HOW IT WORKS
============

Setting up or upgrading a cluster of PCs typically leads to the
problem that many machines need the exact same files. There are
different approaches to distribute the setup of one "master" machine
to all the other machines in the cluster. Our approach is not
sophisticated, but simple and fast (at least for fast switched
networks). We send the data around in a "virtual TCP ring" from the
server to all the clients which store tha received data on their local
disks.

One machine is the master and distributes the data to the others. The
master can be a machine of the cluster or some other machine (in the
current version of dolly it should be the same architecture
though). It stores the image of the partition or disk to be cloned or
has the partition on a local disk. The server should be on a fast
switched network (as all the other machines too) for fast cloning.

All other machines are clients. They receive the data from the ring,
store it to the local disk and send it to the next machine in the
ring. It is important to note that all of this happens at the same
time.

The cloning process is depicted in the following two figures. Usually
there are more than two clients, but you get the idea:

      +========+  +==========+ +==========+
      | Master |  | Client 1 | | Client 2 |
      +====+===+  +===|======+ +====+=====+
            \         |            /
             \    +===+====+      /
              +===+ Switch |=====+
                  +========+

        Cloning process, physical network


     +========+  Data   +==========+  Data  +==========+
     | Master |========>| Client 1 |=======>| Client 2 |
     +========+         +==========+        +==========+
         ^                   |                   |
         | Data              | Data              | Data
         |                   V                   V
      +======+            +======+            +======+
      | Disk |            | Disk |            | Disk |
      +======+            +======+            +======+

     Cloning process, virtual network with TCP connections


We choose this method instead of a multicast scheme because it is
simple to implement, doesn't require the need to write a reliable
multicast protocol and works quite well with existing
technologies. One could also use the master as an NFS server and copy
the data to each client, but this puts quite a high load on the server
and makes it the bottleneck. Furthermore, it would not be possible to
directly clone partitions from one machine to some others without any
filesystem in the partition.


DIFFERENT CLONING POSSIBILITIES
===============================

There are different possibilities to clone your master machine:

- You already have an image of the partition which you want to clone
  on your master (raw or compressed). In this case you need Linux
  (some other UNIX might also work, but we haven't tested that yet) on
  your master and a Linux on each client.

- You want to clone a partition which is on a local disk of your
  master. In this case you need Linux (or probably another UNIX, we
  haven't tried that) on your master as well as on all the clients.
  You can use any Linux installation as long as it's not the one you
  want to clone (i.e. you can not clone the Linux which you are
  currently running in. See the warning below).

- You want to clone a whole disk including all the partitions. In this
  case you either need a second disk on all machines where your Linux
  used for the cloning process runs on (not the one you want to clone)
  or you need a small one-disk-Linux which you boot on all
  machines. In the later case you also need dolly on all machines
  (mount it with NFS) and the config-file on the master.

WARNING: You can NOT clone an OS which is currently in use. That is why
         we have a small second Linux installation on all of our machines
         (or a small system that can be booted over the network by PXE),
         which we can boot to clone our regular Linux partition.


CHANGES 
=======

See CHANGELOG file

TODO
====

Secure transfer of data using SSL?

EXAMPLE
=======

In this example we assume a cluster of 16 machines, named
node0..node15. We want to clone the partition sda5 from node0 to all
other nodes. The configuration file (let's name it dollytab.cfg)
should then look as follows:
```
infile /dev/sda5
outfile /dev/sda5
node1
node2
node3
node4
node5
node6
node7
node8
node9
node10
node11
node12
node13
node14
node15
endconfig
```

Next, we start Dolly on all the clients. No options are required for
the clients (but you might want to add the "-v" option for verbose
progress reports). Finally, Dolly is started on the server as follows:
```sh
dolly -v -s -f dollytab.cfg
```

EXPERIMENTAL
============

Be aware of the following restrictions:
* The output data type must be the same as the input data type. Mixing the type of input and output can lead to data corruption.
* Only clone partitions that are identical in size on the dolly server and the client node.
* Only clone strictly identical storage devices, or corruption can occur.

The following command line parameters are not tested and are provided as experimental::
* -S: Ignoring the FQDN is not supported.
* -6: Using IPv6 is not supported.
* -n: Not doing a sync before exiting is not supported as this can lead to data corruption.

The following configuration file options are not tested are provided as experimental:
* split: Splitting files is not supported (infile or outfile).

Bibliography
============

Felix Rauch, Christian Kurmann, Thomas M. Stricker: <em>Optimizing the
distribution of large data sets in theory and practice</em>. Concurrency
and Computation: Practice and Experience, volume 14, issue 3, pages
165-181, april 2002. (c) John Wiley & Sons, Ltd.

~~Maintained by Felix Rauch.
http://www.cs.inf.ethz.ch/~rauch/
Felix Rauch <rauch@inf.ethz.ch>~~

Maintained by openSUSE community

AUTHORS / CONTRIBUTORS
=======================
* Felix Rauch <rauch@inf.ethz.ch>
* Antoine Ginies <aginies@suse.com>
* Christian Goll <cgoll@suse.com>
