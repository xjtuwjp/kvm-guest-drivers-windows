#include "precomp.h"
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "IsrDpc.tmh"
#endif

BOOLEAN
VIOSerialInterruptIsr(
    IN WDFINTERRUPT Interrupt,
    IN ULONG MessageID)
{
    ULONG          ret;
    PPORTS_DEVICE  pContext = GetPortsDevice(WdfInterruptGetDevice(Interrupt));

    UNREFERENCED_PARAMETER(MessageID);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    if((ret = VirtIODeviceISR(pContext->pIODevice)) > 0)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "Got ISR - it is ours %d!\n", ret);
        WdfInterruptQueueDpcForIsr(Interrupt);
        return TRUE;
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "WRONG ISR!\n");
    return FALSE;
}

VOID
VIOSerialInterruptDpc(
    IN WDFINTERRUPT Interrupt,
    IN WDFOBJECT AssociatedObject)
{
    UINT             i;
    PPORTS_DEVICE    pContext;
    PVIOSERIAL_PORT  port;
    WDFDEVICE        Device;


    ULONG            information;
    NTSTATUS         status;
    PUCHAR           systemBuffer;
    size_t           Length;
    WDFREQUEST       request;

    UNREFERENCED_PARAMETER(AssociatedObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    if(!Interrupt)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_DPC, "Got NULL interrupt object DPC!\n");
        return;
    }
    Device = WdfInterruptGetDevice(Interrupt);
    pContext = GetPortsDevice(Device);

    VIOSerialCtrlWorkHandler(Device);
    for (i = 0; i < pContext->consoleConfig.max_nr_ports; ++i)
    {
        port = VIOSerialFindPortById(Device, i);
        if (port)
        {
           WdfSpinLockAcquire(port->InBufLock);
           if (!port->InBuf)
           {
              port->InBuf = VIOSerialGetInBuf(port);
              TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "%s::%d  port->InBuf = %p\n", __FUNCTION__, __LINE__, port->InBuf);
           }
           if (!port->GuestConnected)
           {
              VIOSerialDiscardPortDataLocked(port);
           }

           if (port->InBuf)
           {
              if (port->PendingReadRequest)
              {
                 request = port->PendingReadRequest;
                 status = WdfRequestUnmarkCancelable(request);
                 if (status != STATUS_CANCELLED)
                 {
                    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,"Got available read request\n");
                    status = WdfRequestRetrieveOutputBuffer(request, 0, &systemBuffer, &Length);
                    if (NT_SUCCESS(status))
                    {
                       port->PendingReadRequest = NULL;
                       information = (ULONG)VIOSerialFillReadBufLocked(port, systemBuffer, Length);
                       WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, information);
                    }
                 }
                 else
                 {
                    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "Request = %p was cancelled\n", request);
                 }
              }
           }
           WdfSpinLockRelease(port->InBufLock);
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

static
VOID
VIOSerialEnableInterrupt(PPORTS_DEVICE pContext)
{

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s enable\n", __FUNCTION__);

    if(!pContext)
        return;

    if(pContext->c_ivq)
    {
        pContext->c_ivq->vq_ops->enable_interrupt(pContext->c_ivq);
        pContext->c_ivq->vq_ops->kick(pContext->c_ivq);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s enable\n", __FUNCTION__);
}

static
VOID
VIOSerialDisableInterrupt(PPORTS_DEVICE pContext)
{

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s disable\n", __FUNCTION__);

    if(!pContext)
        return;

    if(pContext->c_ivq)
    {
        pContext->c_ivq->vq_ops->disable_interrupt(pContext->c_ivq);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s disable\n", __FUNCTION__);
}


NTSTATUS
VIOSerialInterruptEnable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    VIOSerialEnableInterrupt(GetPortsDevice(WdfInterruptGetDevice(Interrupt)));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
VIOSerialInterruptDisable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    VIOSerialDisableInterrupt(GetPortsDevice(WdfInterruptGetDevice(Interrupt)));
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID
VIOSerialEnableInterruptQueue(IN struct virtqueue *vq)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s enable\n", __FUNCTION__);

    if(!vq)
        return;

    vq->vq_ops->enable_interrupt(vq);
    vq->vq_ops->kick(vq);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialDisableInterruptQueue(IN struct virtqueue *vq)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s disable\n", __FUNCTION__);

    if(!vq)
        return;

    vq->vq_ops->disable_interrupt(vq);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
}

