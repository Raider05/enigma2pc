#!/bin/sh
#
# make_release.sh - make and upload a new xine-lib release


this=misc/make_release.sh
tmpfile=/tmp/make_release.$$.tmp

# first of all: check directory structure
#########################################

case "$0" in 
   /*)
      location="$0"
      ;;
   *)
    location=`pwd`/"$0"
    ;;
esac
    
location=`echo "$location"|sed -e 's|//|/|g' -e 's|/./|/|g'`

if test -r "$location"; then
    topdir=`echo "$location"|sed -e 's|/'$this'$||'`
else
    echo 'Help! Unable to find myself.'
    echo 1
fi

cd "$topdir"

if test -r CVS/Root; then
    cvsroot=`cat CVS/Root`
    case "$cvsroot" in
      *cvs.xine.sf.net:/cvsroot/xine*) ;;
      *cvs.xine.sourceforge.net:/cvsroot/xine*) ;;
      *)
        echo "This doesn't look like a xine CVS checkout. aborting..."
	exit 1
	;;
    esac
else
    echo "This must be called from a CVS checkout."
    exit 1
fi

#if [ `basename $topdir` = xine-lib ]; then
#    echo "good, this seems to be a xine-lib CVS checkout."
#else
#    echo "This script is intended to be called from xine-lib CVS trees only."
#    exit 1
#fi

superdir=`dirname "$topdir"`
if test -d "$superdir/xine_www"; then
  echo "okay, found xine_www module. Let's check that..."
  www_dir="$superdir/xine_www"
  cd "$www_dir"
  if test -r CVS/Root && grep cvsroot/xine CVS/Root >/dev/null 2>&1; then
    echo "okay, looks like a xine_www CVS checkout."
  else
    echo "this doesn't seem to be a xine_www CVS checkout. Aborting."
    echo "please retry after checking out like this:"
    echo "cd $superdir; cvs -d cvs.xine.sf.net:/cvsroot/xine co xine_www"
    exit 1
  fi
else
  echo "Unable to find a xine_www checkout in $superdir"
  exit 1
fi


# utility function definitions
###############################
yesno() {
  echo "$* (y/n)?"
  read answer
  case "$answer" in
    y*)
	true;;
    n*)
	false;;
    *)
	echo "'pardon?? neither yes nor no? assuming no..."
 	false;;
  esac
}

cvs_up_check(){
cvs up -dP >$tmpfile 2>&1
merged="`grep ^M\  $tmpfile`"
patched="`grep ^P\  $tmpfile`"
conflict="`grep ^C\  $tmpfile`"
unknown="`grep -v ^M\  $tmpfile|grep -v ^\?\ |grep -v ^cvs\ server:|grep -v ^P\ |grep -v ^C\ `"

if test -n "$conflict"; then
  echo "The following files have had local changes (conflicts) that could"
  echo "NOT be merged by CVS:"
  echo "$conflict"
  echo "This means your local tree is in an inconsistent state."
  echo "Releasing this doesn't make sense, please try again after resolving"
  echo "the conflicts! Stopping here."  
  exit 1
fi

if test -n "$patched"; then
  echo "The following files have been updated by CVS:"
  echo "$patched"
  echo "This means your local tree hasn't been up to date before this update."
  echo -n "Do you want to continue anyway"
  if yesno; then
    echo "Okay, I will continue on your request."
  else
    echo "Okay, stopping here."
    exit 1
  fi
fi

if test -n "$merged"; then
  echo "The following files have had local changes that could be merged by CVS:"
  echo "$merged"
  echo "This probably means your local tree hasn't been commited to CVS yet."
  echo -n "Do you want to continue anyway"
  if yesno; then
    echo "Okay, I will continue on your request."
  else
    echo "Okay, stopping here."
    exit 1
  fi
fi

if test -n "$unknown"; then
  echo "Ooops, CVS said something that I didn't understand:"
  echo "$unknown"
  echo "I have no clue what this means, so you have to decide:"
  echo -n "Do you want to continue anyway"
  if yesno; then
    echo "Okay, I will continue on your request."
  else
    echo "Okay, stopping here."
    exit 1
  fi
fi
}


# the actual work starts here
##############################

echo "updating xine_www from CVS..."
cd "$www_dir"
cvs_up_check
echo "fetching download page from xine's web site..."
rm -f $tmpfile
if wget -nv -O $tmpfile http://xine.sf.net/download.html; then
  echo "diffing against CVS checkout:"
  if diff download.html $tmpfile; then
    echo "Okay, they are the same."
  else
    echo "The CVS version is different from that on the web server!"
    echo "Please fix that and try again!"
    exit 1
  fi
else
  echo "Unable to fetch the download file from http://xine.sf.net; aborting."
  exit 1
fi

lastver=`grep 'href="files/xine-lib-' download.html \
          |sed -e 's|^.*href="files/xine-lib-||' -e 's|\.tar\.gz.*$||g' \
          |head -n 1`

echo "The last release has been $lastver."
old_major=`echo $lastver|awk -F. '{print $1}'`
old_minor=`echo $lastver|awk -F. '{print $2}'`
old_sub=`echo $lastver|awk -F. '{print $3}'`

cd "$topdir"
echo "updating xine-lib from CVS..."
cvs_up_check

new_major=`awk -F= '/XINE_MAJOR=/ {print $2}' <configure.in`
new_minor=`awk -F= '/XINE_MINOR=/ {print $2}' <configure.in`
new_sub=`awk -F= '/XINE_SUB=/ {print $2}' <configure.in`
new_ver="$new_major.$new_minor.$new_sub"

if [ "$new_ver" = "$lastver" ]; then
  echo "According to configure.in, the new release is $new_ver, which is"
  echo "exactly the same as the last release."
  echo "Please update version info (XINE_{MAJOR,MINOR,SUB} as well as the"
  echo "libtool version info (XINE_LT_{CURRENT,REVISION,AGE}) and try again!"
  exit 1
fi

echo "preparing the release tarball..."
rm -f .cvsversion
./cvscompile.sh
echo "making release tarball..."
tarball=xine-lib-${new_ver}.tar.gz
if make distcheck; then
  echo "Please test $tarball now: unpack, configure and make it,"
  echo "run some tests with the resulting installation, and if you think"
  echo "it's okay: type \"exit\" to get back to the release script!"
  ${SHELL:-bash}
else
  echo "make distcheck failed, I'm unable to make a dist tarball."
  echo "Sorry, can't help you any more..."
  exit 1
fi

echo "Is that $tarball okay"
if yesno; then
  echo "good. let's start the upload!"
  echo "adding entry to download page..."
  cd "$www_dir"
  while IFS="" read line; do
    if echo " $line" | grep 'href="files/xine-lib-'$lastver &>/dev/null; then
        echo "        <a href=\"files/$tarball\">$tarball</a><br>"
    fi
    echo "$line"
  done <download.html >download.html.new \
  && mv download.html.new download.html
  
  echo "committing download file to xine_www CVS"
  cvs commit -m "added $tarball" download.html

  cd "$topdir"
  echo "copying the tarball to xine.sf.net..."
  scp "$tarball" xine.sf.net:/home/groups/x/xi/xine/htdocs/files
  
  cd "$www_dir"
  echo "copying download page to xine.sf.net..."
  scp download.html xine.sf.net:/home/groups/x/xi/xine/htdocs
else
  echo "Sorry. Please try again after fixing it!"
  exit 1
fi

cd "$topdir"
echo "press return to clean up (make maintainer-clean; cvs up)..."
read line
make maintainer-clean
cvs up -dP
tag=xine-${new_major}_${new_minor}_${new_sub}-release
echo -n "set CVS tag $tag" 
if yesno; then
  cvs tag $tag
else
  echo "Okay, but you should probably do something like"
  echo "cvs tag $tag"
fi

echo "Well, that's it. xine-lib $new_ver is officially released"
echo "You should probably announce it on xine-announce."

rm $tmpfile
