#ifndef MemoryCardDriverThreaded_Windows_H
#define MemoryCardDriverThreaded_Windows_H 1

#include "MemoryCardDriverThreaded.h"

/* USB memory cards on Windows.  A card is any removable volume that has media in it.
 * It is identified by its drive letter, which is what MemoryCardOsMountPointP1/P2 match
 * ("E:/").  The USB bus, port and level aren't known, so those preferences don't apply. */
class MemoryCardDriverThreaded_Windows : public MemoryCardDriverThreaded
{
public:
	MemoryCardDriverThreaded_Windows();
	virtual ~MemoryCardDriverThreaded_Windows();

	virtual void Unmount( UsbStorageDevice* pDevice, CString sMountPoint );
	virtual void Flush( UsbStorageDevice* pDevice );
protected:
	virtual void Mount( UsbStorageDevice* pDevice, CString sMountPoint );
	virtual void ResetUsbStorage();
	virtual void MountThreadDoOneUpdate();

	/* Windows types are kept out of this header; it's included everywhere. */
	struct Drive
	{
		char cLetter;			// upper case
		unsigned long iVolumeSerial;
		bool bRereadName;
		UsbStorageDevice usbd;
	};

	/* For the mounting thread only. */
	vector<Drive> m_vDrives;
	unsigned long m_iLastLogicalDrives;
	vector<char> m_vRemovableLetters;	// the removable ones among the last logical drives
	int m_iPollsSinceTypeScan;
	char m_cIgnoredDrive;	// the removable drive we said we're ignoring, so we only say it once

	unsigned long m_iOldErrorMode;
	bool m_bRetest;		// set by ResetUsbStorage; protected by m_mutexStorageDevices
};

#endif
