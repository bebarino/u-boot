.. SPDX-License-Identifier: GPL-2.0+
.. sectionauthor:: Stephen Boyd <swboyd@chromium.org>

Chromebook Trogdor/Strongbad
============================

Trogdor and Strongbad are Chromebooks that are either clamshells or
detachables. U-Boot can be installed into the alternate firmware (altfw) slot
in the SPI-NOR flash chip and selected in the developer mode boot screen. This
chain loads U-Boot from Depthcharge and avoids disabling the write protect
screw.

Enable Alternate Firmware
=========================

First, you need to enable developer mode on the Chromebook. On a clamshell
that's done by pressing power + ESC + refresh. On a detachable you press the
power button + volume up + volume down. Once you get to the recovery screen,
press Ctrl+d to enter developer mode.

In developer mode, switch to VT2 (use Ctrl+Alt+refresh) and log in as 'root'
(the default has no password). Configure Depthcharge to load U-Boot::

  cd /tmp
  flashrom -r bios.bin
  cat > ubootfw.txt <<EOF
  0;uboot;uboot;u-boot for coreboot on ARM
  1;uboot;uboot;u-boot for coreboot on ARM
  EOF
  cbfstool /tmp/bios.bin remove -r RW_LEGACY -n altfw/list
  cbfstool bios.bin add -r RW_LEGACY -n altfw/list -f ubootfw.txt -t raw

You can also prevent future ChromeOS firmware updates from replacing the
alternate firmware::

  cbfstool bios.bin remove -r RW_LEGACY -n cros_allow_auto_update

And finally enable the alternate firmware feature::

  crossystem dev_boot_altfw=1

It's a good idea to flash the RW_LEGACY section now with this alternate
firwmare list even though we haven't installed U-Boot yet::

  flashrom -w bios.bin -i RW_LEGACY

Installation
------------

Build U-Boot and obtain u-boot.elf::

   $ make chromebook_trogdor_defconfig
   $ make

Copy the 'u-boot.elf' file to the Chromebook. On the Chromebook, add the u-boot
payload::

  cd /tmp
  flashrom -r bios.bin
  cbfstool bios.bin add-payload -r RW_LEGACY -c lzma -n uboot -f u-boot.elf
  flashrom -w bios.bin -i RW_LEGACY

Now you can reboot and select the "Select alternate bootloader" option. Select
'uboot' to boot U-Boot. If you would like this to be the default boot option,
you can boot into ChromeOS and switch to VT2 (use Ctrl+Alt+refresh) to use
crossystem::

  crossystem dev_default_boot=altfw

