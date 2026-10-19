#!/bin/sh
# Builds with Visual Studio 2022 for x64. VS170COMNTOOLS must point at its
# Common7/Tools directory.

function Usage()
{
  echo "Usage:"
  echo -e "\t$1 [-conf "'"Release"|"Debug"'"] [-action build|clean|rebuild] [-proj projects] [-solution sln_file]"
  echo -e "\nDefault:"
  echo -e '-conf\t\t"Release"'
  echo -e '-action\t\tbuild'
  echo -e '-proj\t\t"vsfilter"'
  echo -e '-solution\tVSFilter.sln'
}

script_dir=`dirname $0`
cd $script_dir

solution="VSFilter.sln"
action="build"
configuration="Release"
projects="vsfilter"

while [ "$1"x != ""x ]
do
  if [ "$flag"x == ""x ]; then
    if [ "$1"x == "-configuration"x ] || [ "$1"x == "-conf"x ]; then
      flag="configuration"
    elif [ "$1"x == "-action"x ]; then
      flag="action"
    elif [ "$1"x == "-projects"x ] || [ "$1"x == "-proj"x ]; then
      flag="projects"
    elif [ "$1"x == "-solution"x ]; then
      flag="solution"
    else
      echo "Invalid arguments"
      Usage $0
      exit -1
    fi
  else
    if [ "${1:0:1}"x == "-"x ]; then
      echo "Invalid arguments"
      Usage $0
      exit -1
    fi
    eval $flag='"'$1'"'
    flag=""
  fi
  shift
done

if [ "$flag"x != ""x ]; then
  echo "Invalid arguments"
  Usage $0
  exit -1
fi

action=`echo $action | awk '{ print tolower($0) }'`
if [ "$action"x == "build"x ]; then
  action=""
else
  action=":"$action
fi

for project in $projects
do
echo '
CALL "%VS170COMNTOOLS%../../VC/Auxiliary/Build/vcvarsall.bat" x86_amd64
msbuild /m /t:'$project''$action' /p:Configuration='$configuration' /p:Platform=x64 /p:BuildProjectReferences=false "'$solution'"
exit %ERRORLEVEL%
' | cmd || exit 1
done
