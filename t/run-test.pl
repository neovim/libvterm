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
      print "> $1\n";
   }

   # Commands have capitals
   elsif( $line =~ m/^([A-Z]+)/ ) {
      # Some convenience formatting
      if( $line =~ m/^PUSH (.*)$/ ) {
         # we're evil
         my $string = eval($1);
         $line = "PUSH " . unpack "H*", $string;
      }

      do_onetest if defined $command;

      $command = $line;
      undef @expect;
   }
   # Expectations have lowercase
   elsif( $line =~ m/^([a-z]+)/ ) {
      # Convenience formatting
      if( $line =~ m/^text (.*)$/ ) {
         $line = "text " . join ",", map sprintf("%x", $_), eval($1);
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
      else {
         warn "Unrecognised test expectation '$line'\n";
      }

      push @expect, $line;
   }
}

do_onetest if defined $command;

exit $exitcode;
