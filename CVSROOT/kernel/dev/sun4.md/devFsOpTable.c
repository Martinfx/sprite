/* 
 * devFsOpTable.c --
 *
 *	The operation tables for the file system devices on Sun-4 hosts.
 *
 * Copyright 1987, 1988 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header$ SPRITE (Berkeley)";
#endif not lint


#include "sprite.h"
#include "dev.h"
#include "devInt.h"
#include "fs.h"
#include "rawBlockDev.h"
#include "devFsOpTable.h"
#include "devTypes.h"

/*
 * Device specific include files.
 */

#include "devSyslog.h"
#include "devNull.h"
#include "devSCSIDisk.h"
#include "devSCSITape.h"
#include "devSCSIRobot.h"
#include "xylogics450.h"
#include "devNet.h"
#include "devBlockDevice.h"
#include "scsiHBADevice.h"
#include "raidExt.h"
#include "tty.h"
#include "mouse.h"
#include "devTMR.h"
#include "devfb.h"
#include "devVMElink.h"
#include "atc.h"
#include "devSmem.h"
#include "devXbus.h"

static ReturnStatus NoDevice();
static ReturnStatus NullProc();


/*
 * Device type specific routine table:
 *	This is for the file-like operations as they apply to devices.
 *	DeviceOpen
 *	DeviceRead
 *	DeviceWrite
 *	DeviceIOControl
 *	DeviceClose
 *	DeviceSelect
 *	BlockDeviceAttach
 *	DeviceReopen
 *	DeviceMmap
 */


DevFsTypeOps devFsOpTable[] = {
    /*
     * Serial lines used to implement terminals.
     */
    {DEV_TERM,       DevTtyOpen, DevTtyRead, DevTtyWrite,
		     DevTtyIOControl, DevTtyClose, DevTtySelect,
		     DEV_NO_ATTACH_PROC, NoDevice, NullProc},
    /*
     * The system error log.  If this is not open then error messages go
     * to the console.
     */
    {DEV_SYSLOG,    Dev_SyslogOpen, Dev_SyslogRead, Dev_SyslogWrite,
		    Dev_SyslogIOControl, Dev_SyslogClose, Dev_SyslogSelect,
		    DEV_NO_ATTACH_PROC, Dev_SyslogReopen, NullProc},
    /*
     * SCSI Worm interface:  this device doesn't exist anymore.
     */
    {DEV_SCSI_WORM, Dev_TimerOpen, Dev_TimerRead, NullProc,
                    Dev_TimerIOControl, NullProc, NullProc,
                    DEV_NO_ATTACH_PROC, NoDevice, NullProc},
    /*
     * The following device number is unused.
     */
    {DEV_PLACEHOLDER_2, NoDevice, NullProc, NullProc,
		    Dev_NullIOControl, NullProc, NullProc,
		    DEV_NO_ATTACH_PROC, NoDevice, NullProc},
    /*
     * New SCSI Disk interface.
     */
    {DEV_SCSI_DISK, DevRawBlockDevOpen, DevRawBlockDevRead,
		    DevRawBlockDevWrite, DevRawBlockDevIOControl, 
		    DevRawBlockDevClose, NullProc, DevScsiDiskAttach,
		    DevRawBlockDevReopen, NullProc},
    /*
     * SCSI Tape interface.
     */
    {DEV_SCSI_TAPE, DevSCSITapeOpen, DevSCSITapeRead, DevSCSITapeWrite,
		    DevSCSITapeIOControl, DevSCSITapeClose, NullProc,
		    DEV_NO_ATTACH_PROC, NoDevice, NullProc},
    /*
     * /dev/null
     */
    {DEV_MEMORY,    NullProc, Dev_NullRead, Dev_NullWrite,
		    Dev_NullIOControl, NullProc, Dev_NullSelect,
		    DEV_NO_ATTACH_PROC, NullProc, NullProc},
    /*
     * Xylogics 450 disk controller.
     */
    {DEV_XYLOGICS, DevRawBlockDevOpen, DevRawBlockDevRead,
		    DevRawBlockDevWrite, DevRawBlockDevIOControl, 
		    DevRawBlockDevClose, NullProc, DevXylogics450DiskAttach,
		    DevRawBlockDevReopen, NullProc},
    /*
     * Network devices.  The unit number specifies the ethernet protocol number.
     */
    {DEV_NET,      DevNet_FsOpen, DevNet_FsRead, DevNet_FsWrite, 
		   DevNet_FsIOControl, DevNet_FsClose, DevNet_FsSelect, 
		   DEV_NO_ATTACH_PROC, DevNet_FsReopen, NullProc},
    /*
     * Raw SCSI HBA interface.
     */
    {DEV_SCSI_HBA, DevSCSIDeviceOpen, Dev_NullRead, Dev_NullWrite,
		    DevSCSIDeviceIOControl, DevSCSIDeviceClose, Dev_NullSelect,
		    DEV_NO_ATTACH_PROC, NoDevice, NullProc},
    /*  
     * RAID device.
     */ 
    {DEV_RAID, DevRawBlockDevOpen, DevRawBlockDevRead,
                    DevRawBlockDevWrite, DevRawBlockDevIOControl,
                    DevRawBlockDevClose, NullProc, DevRaidAttach,
                    DevRawBlockDevReopen, NullProc},
    /*  
     * Debug device. (useful for debugging RAID device)
     */ 
    {DEV_DEBUG, DevRawBlockDevOpen, DevRawBlockDevRead,
                    DevRawBlockDevWrite, DevRawBlockDevIOControl,
                    DevRawBlockDevClose, NullProc, DevDebugAttach,
                    DevRawBlockDevReopen, NullProc},
    /*
     * Event devices for window systems.
     */
    {DEV_MOUSE,    DevMouseOpen, DevMouseRead, DevMouseWrite,
		   DevMouseIOControl, DevMouseClose, DevMouseSelect,
		   DEV_NO_ATTACH_PROC, NoDevice, NullProc},
    /*
     * Frame buffer device.
     */
    {DEV_GRAPHICS, DevFBOpen, NullProc, NullProc,
                   DevFBIOControl, DevFBClose, NullProc,
                   DEV_NO_ATTACH_PROC, NoDevice, DevFBMMap},

    /*
     * /dev/smem
     */
    {DEV_SMEM,     NullProc, Dev_SmemRead, Dev_SmemWrite,
                   Dev_SmemIOControl, NullProc, Dev_SmemSelect,
                   DEV_NO_ATTACH_PROC, NullProc, NullProc},
     
    /*
     * The audio device may exist for the sun4.  If so, ignore it for now.
     */
    {DEV_AUDIO, NoDevice, NullProc, NullProc,
		Dev_NullIOControl, NullProc, NullProc,
		DEV_NO_ATTACH_PROC, NoDevice, NullProc},
    /*
     * VME link board device.
     */
    {DEV_VMELINK, DevVMElinkOpen, DevVMElinkRead, DevVMElinkWrite,
	 	  DevVMElinkIOControl, NullProc, NullProc,
	 	  DEV_NO_ATTACH_PROC, NoDevice},
    /*
     * These devices don't exist on a sun4.
     */
    {17, NullProc, NullProc, NullProc, NullProc, NullProc, NullProc, 
		   DEV_NO_ATTACH_PROC, NullProc, NullProc},
    /*
     * The following device number is unused.
     */
    {DEV_ATC, DevRawBlockDevOpen, DevRawBlockDevRead,
                    DevRawBlockDevWrite, DevRawBlockDevIOControl,
                    DevRawBlockDevClose, NullProc, DevATCDiskAttach,
                    DevRawBlockDevReopen, NullProc},
    /*
     * The following device number is unused.
     */
    {DEV_PLACEHOLDER_5, NoDevice, NullProc, NullProc,
		    NullProc, NullProc, NullProc, 
		    DEV_NO_ATTACH_PROC, NoDevice, NullProc},
    /*
     * The "jukebox", or tape/disk robot device.
     */
    {DEV_SCSI_ROBOT, DevSCSIExbRobotOpen, NullProc, NullProc,
	            DevSCSIExbRobotIOControl, DevSCSIExbRobotClose,
	            NullProc, DEV_NO_ATTACH_PROC, NoDevice, NullProc},
    /*
     * Xbus board
     */
    {DEV_XBUS, NullProc, NullProc, NullProc, DevXbusIOControl,
	 NullProc, NullProc, DEV_NO_ATTACH_PROC, NoDevice, NullProc},
};

int devNumDevices = sizeof(devFsOpTable) / sizeof(DevFsTypeOps);

static ReturnStatus
NullProc()
{
    return(SUCCESS);
}


static ReturnStatus
NoDevice()
{
    return(FS_INVALID_ARG);
}
