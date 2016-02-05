#!/bin/bash -e

echo "* Installing dependencies"
sudo apt-get -q install git make g++ libstdc++-4.8-dev libelf-dev libtinfo-dev re2c libbsd-dev gfortran build-essential

echo "* Cloning HSA repositories into ~/git/hsa/..."
REPO_URLS="https://github.com/HSAfoundation/HSA-Runtime-AMD.git https://github.com/HSAfoundation/HSAIL-HLC-Stable.git https://github.com/HSAfoundation/CLOC.git https://github.com/HSAfoundation/Okra-Interface-to-HSA-Device"

cd ~/git
mkdir -p hsa
cd hsa
for repo_url in $REPO_URLS; do
  repo_name=$(basename $repo_url)
  repo_name=${repo_name%.*} 
  echo "  - $repo_name"
  if [ ! -d "$repo_name" ]; then
    git clone $repo_url
  fi
done

echo "* Installing HSA Runtime"
sudo mkdir -p /opt/hsa/lib
cd ~/git/hsa/HSA-Runtime-AMD
sudo cp -R include /opt/hsa
sudo cp lib/* /opt/hsa/lib

echo "* Installing HSAIL Compiler"
cd ~/git/hsa/HSAIL-HLC-Stable
sudo mkdir -p /opt/amd
sudo cp -R bin /opt/amd

echo "* Installing CLOC Compiler"
cd ~/git/hsa/CLOC
sudo cp bin/cloc /usr/local/bin/.
sudo cp bin/cloc_genw /usr/local/bin/.

#echo "* Installing OKRA"
#cd ~/git/hsa/Okra-Interface-to-HSA-Device
#sudo cp -r okra /opt/amd
#sudo cp okra/dist/bin/libokra_x86_64.so /opt/hsa/lib/.

