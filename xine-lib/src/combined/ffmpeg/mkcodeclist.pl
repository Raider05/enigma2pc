#! /usr/bin/perl -w

# Make codec lists for #inclusion by ff_*_decoder.c.
# Parameters:
#	list of ffmpeg CODEC_ID_* (pre-processed, one per line)
#	list of codecs recognised by xine-lib (see list for details)
#	output file name, or "-" to generate a report on unhandled codecs

my ($ffmpeg, $xine, $out) = @ARGV;
my $line;

# Read in the ffmpeg codec IDs
my %codecs;
open LIST, "< $ffmpeg" or die $!;
$line = <LIST>;
while (defined $line) {
  chomp $line;
  $line =~ s/^CODEC_ID_//o;
  $codecs{$line} = 0;
  $line = <LIST>;
}
close LIST or die $!;

# Read in the xine-lib codec IDs
my %config;
my @known;
my $type = 'audio'; # default type
my $Type = 'AUDIO';
my ($a, $f, $t);
open LIST, "< $xine" or die $!;
while (defined ($line = <LIST>)) {
  next if substr ($line, 0, 1) eq '#' or $line =~ /^\s*$/o;
  chomp $line;
  if (substr ($line, 0, 5) eq 'type=') {
    # codec type; "FOO" in "BUF_FOO_BAR"
    $type = substr ($line, 5);
    $type =~ tr/A-Z/a-z/;
    $Type = $type;
    $Type =~ tr/a-z/A-Z/;
  } elsif (substr ($line, 0, 7) eq 'config=') { 
    # avcodec minimum version mappings
    ($a, $f, $t) = split (/=/, $line, 3);
    $config{$f} = $t if $t =~ /^\d+,\d+,\d+$/
  } else {
    # codec details
    push @known, [split (/\s+/, $line, 3)];
  }
}
close LIST or die $!;

# Look through the mappings.
# Mark what we can handle and report on what the installed ffmpeg can't
foreach $line (@known) {
  if (defined $codecs{$line->[1]}) {
    ++$codecs{$line->[1]};
  } else {
    print "Ignored $line->[0] = $line->[1]\n";
  }
}

my $w = ($out ne '-');

if ($w) {
  # Write the C source code for the codec lists
  open LIST, "> $out" or die $!;
  print LIST "#ifndef AV_VERSION_INT\n# define AV_VERSION_INT(a,b,c) 0x7FFFFFFF\n#endif\n" or die $!;
  print LIST "static const ff_codec_t ff_${type}_lookup[] = {\n" or die $!;
  foreach $line (@known) {
    next if $line->[0] eq '!';
    next unless defined $codecs{$line->[1]};
    print LIST "  { BUF_${Type}_$line->[0], CODEC_ID_$line->[1], \"$line->[2] (ffmpeg)\" },\n" or die $!;
  }
  print LIST "};\n\nstatic uint32_t supported_${type}_types[] = {\n" or die $!;
  foreach $line (@known) {
    next if $line->[0] eq '!';
    next unless defined $codecs{$line->[1]};
    $a = '';
    $a = $config{$a} if defined $config{$a};
    if ($a eq '') {
      print LIST "  BUF_${Type}_$line->[0],\n" or die $!;
    } else {
      print LIST "  #if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT($a)\n  BUF_${Type}_$line->[0],\n  #endif\n" or die $!;
    }
  }
  print LIST "  0,\n};\n" or die $!;
  close LIST or die $!;
}
else {
  # Report on ffmpeg codecs which we don't handle
  print "Unhandled $type codecs:\n";
  foreach $line (sort keys %codecs) {
    print "  $line\n" if $codecs{$line} == 0;
  }
}

exit 0;
