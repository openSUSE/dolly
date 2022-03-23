#!/usr/bin/perl
# quick way of sending multiple files to multiple nodes
#
use strict;

# basic dolly stuff
my $DOLLY = "/usr/sbin/dolly";
my $ARGS = "-dqs";

# list of nodes, adjust to your setup
my @NODES = qw(sle15sp42
       	sle15sp43
       	sle15sp44
	);

# files to transfert, adjust with your needs
# by default the output name will be the same as the input name 
my @INFILES = qw(/tmp/warewulf4-4.2.0-lp154.6.3.x86_64.rpm
		/tmp/warewulf4-overlay-4.2.0-lp154.6.3.x86_64.rpm
		);

# transfert all files from a directory
# !! the directory must be present on the client nodes
my @ALL_FILES_IN_DIR = glob("/tmp/*.*");

my @ALL = (@INFILES, @ALL_FILES_IN_DIR);

my $NODES;
foreach my $n (@NODES) { 
	$n and $NODES = "$n" . "," . $NODES; 
	}
# remove last comma
chop($NODES);

foreach my $INFILE (@ALL) {
	print("---------------------------------\n");
	print("Sending $INFILE to $NODES\n");
	# same input and output name
	my $OUTFILE= $INFILE;
	#print("$DOLLY $ARGS -H $NODES -I $INFILE -O $OUTFILE\n");
	system("$DOLLY $ARGS -H $NODES -I $INFILE -O $OUTFILE");
	sleep 1;
}
