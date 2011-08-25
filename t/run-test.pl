#!/usr/bin/perl

use strict;
use warnings;
use IO::Handle;
use IPC::Open2 qw( open2 );

my ( $hin, $hout );
{
   local $ENV{LD_LIBRARY_PATH} = ".";
   open2 $hout, $hin, "t/harness" or die "Cannot open2 harness - $!";
}

my $exitcode = 0;

my $command;
my @expect;

sub do_onetest
{
   $hin->print( "$command\n" );
   undef $command;

   my $fail_printed = 0;

   while( my $outline = <$hout> ) {
      last if $outline eq "DONE\n" or $outline eq "?\n";

      chomp $outline;

      if( !@expect ) {
         print "# Test failed\n" unless $fail_printed++;
         print "#    expected nothing more\n" .
               "#   Actual:   $outline\n";
         next;
      }

      my $expectation = shift @expect;

      next if $expectation eq $outline;

      print "# Test failed\n" unless $fail_printed++;
      print "#   Expected: $expectation\n" .
            "#   Actual:   $outline\n";
   }

   if( @expect ) {
      print "# Test failed\n" unless $fail_printed++;
      print "#   Expected: $_\n" .
            "#    didn't happen\n" for @expect;
   }

   $exitcode = 1 if $fail_printed;
}

open my $test, "<", $ARGV[0] or die "Cannot open test script $ARGV[0] - $!";

while( my $line = <$test> ) {
   $line =~ s/^\s+//;
   next if $line =~ m/^(?:#|$)/;

   chomp $line;

   if( $line =~ m/^!(.*)/ ) {
      do_onetest if defined $command;
      print "> $1\n";
   }

   # Commands have capitals
   elsif( $line =~ m/^([A-Z]+)/ ) {
      # Some convenience formatting
      if( $line =~ m/^(PUSH|ENCIN|INSTR \d) (.*)$/ ) {
         # we're evil
         my $string = eval($2);
         $line = "$1 " . unpack "H*", $string;
      }

      do_onetest if defined $command;

      $command = $line;
      undef @expect;
   }
   # Expectations have lowercase
   elsif( $line =~ m/^([a-z]+)/ ) {
      # Convenience formatting
      if( $line =~ m/^(text|encout|output) (.*)$/ ) {
         $line = "$1 " . join ",", map sprintf("%x", $_), eval($2);
      }
      elsif( $line =~ m/^control (.*)$/ ) {
         $line = sprintf "control %02x", eval($1);
      }
      elsif( $line =~ m/^escape (.*)$/ ) {
         $line = sprintf "escape %02x", eval($1);
      }
      elsif( $line =~ m/^csi (\S+) (.*)$/ ) {
         $line = sprintf "csi %02x %s", eval($1), $2; # TODO
      }
      elsif( $line =~ m/^osc (.*)$/ ) {
         $line = "osc " . join "", map sprintf("%02x", $_), unpack "C*", eval($1);
      }
      elsif( $line =~ m/^putglyph (\S+) (.*)$/ ) {
         $line = "putglyph " . join( ",", map sprintf("%x", $_), eval($1) ) . " $2";
      }
      elsif( $line =~ m/^(?:moverect|erase|damage) / ) {
         # no conversion
      }
      else {
         warn "Unrecognised test expectation '$line'\n";
      }

      push @expect, $line;
   }
   # Assertions start with '?'
   elsif( $line =~ s/^\?([a-z]+.*?=)\s+// ) {
      do_onetest if defined $command;

      my ( $assertion ) = $1 =~ m/^(.*)\s+=/;

      $hin->print( "\?$assertion\n" );
      my $response = <$hout>;
      chomp $response;

      if( $response ne $line ) {
         print "# Assert $assertion failed:\n" .
               "# Expected: $line\n" .
               "# Actual:   $response\n";
         $exitcode = 1;
      }
   }
   else {
      die "Unrecognised TEST line $line\n";
   }
}

do_onetest if defined $command;

exit $exitcode;
