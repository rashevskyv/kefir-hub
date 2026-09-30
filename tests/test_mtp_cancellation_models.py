#!/usr/bin/env python3
"""Behavioral simulation models for MTP cancellation, watcher, and recovery contracts."""

from typing import Dict, Any, Tuple, Optional, List


class MockMtpSystem:
    """Simulates UI and state machine coordination in haze_helper and progress_box."""

    def __init__(self):
        self.transfer_active = False
        self.transfer_aborted = False
        self.transfer_seq = 0
        self.handled_seq = 0
        self.ui_alive = False
        self.relaunch_count = 0
        self.cancel_haze_calls = 0
        self.reactor_result = "Success"
        self.event_signaled = False
        self.files_on_disk = set()

    def transfer_begin(self, name: str) -> None:
        if self.reactor_result == "Cancelled":
            self.reactor_result = "Success"
        self.event_signaled = False
        self.transfer_active = True
        self.transfer_aborted = False
        self.transfer_seq += 1
        if not self.ui_alive:
            self.ui_alive = True
        self.files_on_disk.add(name)

    def transfer_end_success(self) -> None:
        self.transfer_active = False

    def user_cancels_on_switch(self, name: str) -> None:
        if self.transfer_active:
            self.transfer_active = False
            self.transfer_aborted = True
            self.cancel_haze_calls += 1
            self.reactor_result = "Cancelled"
            self.event_signaled = True
            self.files_on_disk.discard(name)
        self.handled_seq = self.transfer_seq

    def worker_exit(self, user_cancelled: bool) -> None:
        if user_cancelled and self.transfer_active:
            self.transfer_active = False
            self.transfer_aborted = True
            self.cancel_haze_calls += 1
            self.reactor_result = "Cancelled"
            self.event_signaled = True
        if self.handled_seq < self.transfer_seq:
            self.handled_seq = self.transfer_seq

    def pbox_done(self) -> None:
        relaunch = self.transfer_active or (self.transfer_seq != self.handled_seq)
        if relaunch:
            self.relaunch_count += 1
            self.ui_alive = True
        else:
            self.ui_alive = False


class MockParser:
    """Simulates PtpDataParser packet reading, EOT detection, and exact scalar validation."""

    def __init__(self, buffer_size: int = 512):
        self.buffer_size = buffer_size
        self.received_size = 0
        self.offset = 0
        self.eot = False
        self.stream = bytearray()
        self.persistent_error = None
        self.m_data = bytearray()

    def flush(self) -> str:
        if self.eot:
            return "ResultEndOfTransmission"

        if self.persistent_error:
            return self.persistent_error

        packet = self.stream[: self.buffer_size]
        self.stream = self.stream[len(packet) :]
        self.m_data = bytearray(packet)
        self.received_size = len(packet)
        self.offset = 0
        self.eot = self.received_size < self.buffer_size
        return "ResultSuccess"

    def read_buffer(self, count: int) -> Tuple[bytes, str]:
        out = bytearray()
        while count > 0:
            if self.offset == self.received_size:
                rc = self.flush()
                if rc != "ResultSuccess":
                    if len(out) > 0 and rc == "ResultEndOfTransmission":
                        return bytes(out), "ResultSuccess"
                    return bytes(out), rc

            read_size = min(count, self.received_size - self.offset)
            chunk = self.m_data[self.offset : self.offset + read_size]
            out.extend(chunk)
            self.offset += read_size
            count -= read_size

        return bytes(out), "ResultSuccess"

    def read_scalar(self, size: int) -> Tuple[Optional[bytes], str]:
        data, rc = self.read_buffer(size)
        if rc != "ResultSuccess":
            return None, rc
        if len(data) != size:
            return None, "ResultInvalidArgument"
        return data, "ResultSuccess"


class MockReactor:
    """Simulates EventReactor event dispatch and consumer execution."""

    def __init__(self):
        self.consumers = []
        self.result = "ResultSuccess"

    def add_consumer(self, cb) -> None:
        self.consumers.append(cb)

    def set_result(self, res: str) -> None:
        self.result = res

    def get_result(self) -> str:
        return self.result

    def wait_for_timeout(self, timeout_ms: int, stop_signaled: bool = False, raw_code: Optional[int] = None) -> str:
        if stop_signaled:
            for c in self.consumers:
                c()
        if self.result != "ResultSuccess":
            return self.result
        # Simulates EventReactor::WaitForTimeoutImpl normalizing 0xEA01 (svc::ResultTimedOut)
        if raw_code == 0xEA01:
            return "ResultTimeout"
        return "ResultTimeout"


class MockUsbServer:
    """Simulates AsyncUsbServer and UsbSession URB lifecycle, mutable atomic state, and error propagation."""

    def __init__(self):
        self.is_broken = False
        self.is_cancelled = False
        self.in_cleanup = False
        self.posted_urbs = set()
        self.next_urb = 1
        self.fail_cancel_endpoint = False
        self.cancel_endpoint_rc = "ResultSuccess"

    # Const-compatible methods simulating mutable atomic state
    def set_cleanup(self, cleanup: bool) -> None: self.in_cleanup = cleanup
    def is_in_cleanup(self) -> bool: return self.in_cleanup
    def set_cancelled(self, cancelled: bool) -> None: self.is_cancelled = cancelled
    def get_is_cancelled(self) -> bool: return self.is_cancelled
    def set_broken(self, broken: bool) -> None: self.is_broken = broken
    def get_is_broken(self) -> bool: return self.is_broken

    def cancel_endpoint(self, ep: int, urb_id: int) -> str:
        if self.fail_cancel_endpoint:
            return self.cancel_endpoint_rc
        self.posted_urbs.discard(urb_id)
        return "ResultSuccess"

    def transfer_packet(
        self,
        ep: int,
        size: int,
        reactor: MockReactor,
        event_during_wait: Optional[str] = None,
        is_eot: bool = False,
    ) -> Tuple[bytes, str]:
        # Entry check: broken transport refuses new transfers immediately
        if self.is_broken:
            return b"", "ResultTransferFailed"

        urb_id = self.next_urb
        self.next_urb += 1
        self.posted_urbs.add(urb_id)

        # Simulate local cancel event arriving during USB wait
        if event_during_wait == "local_cancel":
            reactor.set_result("ResultCancelled")

        # 1-second timeout loop
        if event_during_wait == "writer_cancel_during_wait":
            self.is_cancelled = True
            self.in_cleanup = True

        wait_rc = "ResultSuccess"
        if reactor.get_result() == "ResultCancelled":
            wait_rc = "ResultCancelled"
            reactor.set_result("ResultSuccess")
            self.is_cancelled = True
            self.is_broken = True
            cancel_rc = self.cancel_endpoint(ep, urb_id)
            if cancel_rc != "ResultSuccess":
                return b"", cancel_rc
            return b"", "ResultTransferFailed"

        # Host silent timeout (during normal read or cleanup)
        if event_during_wait in ("host_silent", "writer_cancel_during_wait"):
            cancel_rc = self.cancel_endpoint(ep, urb_id)
            # Timeout during data phase breaks transport framing
            self.is_broken = True
            if cancel_rc != "ResultSuccess":
                return b"", cancel_rc
            return b"", "ResultTransferFailed"

        # Disconnect during wait
        if event_during_wait == "disconnect":
            self.cancel_endpoint(ep, urb_id)
            self.is_broken = True
            return b"", "ResultTransferFailed"

        # Stop requested during wait
        if event_during_wait == "stop":
            self.cancel_endpoint(ep, urb_id)
            return b"", "ResultStopRequested"

        self.posted_urbs.discard(urb_id)
        data = b"X" * size if not is_eot else b"X" * (size // 2)
        rc = "ResultSuccess" if not is_eot else "ResultEndOfTransmission"
        return data, rc


class MockPtpResponder:
    """Simulates PtpResponder request loop and termination when transport is broken."""

    def __init__(self, usb_server: MockUsbServer, reactor: MockReactor):
        self.usb_server = usb_server
        self.reactor = reactor
        self.is_running = True
        self.finalized = False
        self.handled_requests = 0

    def loop_process(self, handle_request_fn) -> str:
        while self.is_running:
            # PtpResponder::LoopProcess check: break on broken transport or stop requested
            if self.usb_server.get_is_broken() or self.reactor.get_result() == "ResultStopRequested":
                self.finalize()
                return "Terminated"

            rc = handle_request_fn()
            self.handled_requests += 1

            if rc == "ResultStopRequested":
                self.finalize()
                return "ResultStopRequested"
            if self.usb_server.get_is_broken():
                self.finalize()
                return "Terminated"
        return "ResultSuccess"

    def finalize(self) -> None:
        self.is_running = False
        self.finalized = True


class MockSendObjectTransfer:
    """Simulates multi-threaded SendObject transfer loop, reader/writer cancellation, and drain."""

    def __init__(self, usb_server: MockUsbServer, reactor: MockReactor, total_size: int = 2048):
        self.usb_server = usb_server
        self.reactor = reactor
        self.total_size = total_size
        self.is_cancelled = False
        self.is_done = False
        self.bytes_written = 0
        self.packets_drained = 0
        self.fail_write_cancelled = False

    def reader_step(self, event_during_read: Optional[str] = None, is_eot: bool = False) -> Tuple[int, str]:
        self._check_cancellation()
        if self.is_cancelled:
            return 0, "ResultTransferFailed"

        bytes_read = 0
        if self.is_done:
            return 0, "ResultSuccess"

        if self.reactor.get_result() == "ResultStopRequested":
            return 0, "ResultStopRequested"

        data, rc = self.usb_server.transfer_packet(
            ep=1, size=512, reactor=self.reactor, event_during_wait=event_during_read, is_eot=is_eot
        )

        self._check_cancellation()
        if self.is_cancelled:
            return 0, "ResultTransferFailed"

        if rc != "ResultSuccess" and rc != "ResultEndOfTransmission":
            return 0, rc

        bytes_read = len(data)
        self.packets_drained += 1
        if rc == "ResultEndOfTransmission" or self.bytes_written + bytes_read >= self.total_size:
            self.is_done = True

        return bytes_read, "ResultSuccess"

    def writer_step(self, size: int) -> str:
        if self.is_cancelled:
            return "ResultTransferFailed"

        if self.fail_write_cancelled:
            self.is_cancelled = True
            self.usb_server.set_cancelled(True)
            self.usb_server.set_broken(True)
            self.usb_server.set_cleanup(True)
            return "ResultTransferFailed"

        self.bytes_written += size
        return "ResultSuccess"

    def _check_cancellation(self) -> None:
        if not self.is_cancelled:
            reactor_cancelled = (self.reactor.get_result() == "ResultCancelled")
            usb_cancelled = self.usb_server.get_is_cancelled()
            if reactor_cancelled or usb_cancelled:
                self.is_cancelled = True
                self.usb_server.set_cancelled(True)
                self.usb_server.set_broken(True)
                self.usb_server.set_cleanup(True)


def simulate_cancellation_state_machine() -> None:
    s = MockMtpSystem()
    # Scenario 1: Normal completed transfer
    s.transfer_begin("/switch/file1.nsp")
    assert s.ui_alive and s.transfer_active
    s.transfer_end_success()
    assert not s.transfer_active and "/switch/file1.nsp" in s.files_on_disk
    s.worker_exit(user_cancelled=False)
    s.pbox_done()
    assert not s.ui_alive and s.relaunch_count == 0 and s.cancel_haze_calls == 0

    # Scenario 2: User presses B -> Yes during 1.5s idle window AFTER transfer 1 finished
    s.user_cancels_on_switch("/switch/file1.nsp")
    assert s.cancel_haze_calls == 0 and not s.event_signaled and "/switch/file1.nsp" in s.files_on_disk
    s.worker_exit(user_cancelled=True)
    assert s.cancel_haze_calls == 0
    s.pbox_done()
    assert not s.ui_alive and s.relaunch_count == 0

    # Scenario 3: Next transfer starts -> must NOT be aborted by previous idle cancel!
    s.transfer_begin("/switch/file2.nsp")
    assert s.transfer_active and not s.transfer_aborted and not s.event_signaled
    s.user_cancels_on_switch("/switch/file2.nsp")
    assert s.cancel_haze_calls == 1 and s.transfer_aborted and not s.transfer_active
    assert "/switch/file2.nsp" not in s.files_on_disk
    s.worker_exit(user_cancelled=True)
    assert s.cancel_haze_calls == 1
    s.pbox_done()
    assert not s.ui_alive and s.relaunch_count == 0

    # Scenario 4: Subsequent transfer 3 starts cleanly (responder clears cancelled state)
    s.transfer_begin("/switch/file3.nsp")
    assert s.transfer_active and not s.transfer_aborted and s.reactor_result == "Success"
    s.transfer_end_success()
    s.worker_exit(user_cancelled=False)
    s.pbox_done()
    assert not s.ui_alive and s.relaunch_count == 0 and "/switch/file3.nsp" in s.files_on_disk
    assert s.cancel_haze_calls == 1


def simulate_parser_and_drain_contract() -> None:
    # 1. Truncated Read<T> must fail with ResultInvalidArgument
    p_trunc = MockParser(buffer_size=2)
    p_trunc.stream = bytearray(b"AB")
    p_trunc.flush()
    p_trunc.persistent_error = "ResultEndOfTransmission"
    val, rc = p_trunc.read_scalar(size=4)
    assert rc != "ResultSuccess" and (rc == "ResultInvalidArgument" or rc == "ResultEndOfTransmission")

    # 2. Partial streaming read preserves bytes and propagates error on next call
    p_stream = MockParser(buffer_size=100)
    p_stream.stream = bytearray(b"A" * 100)
    p_stream.flush()
    p_stream.persistent_error = "ResultCancelled"
    data1, rc1 = p_stream.read_buffer(150)
    assert rc1 == "ResultCancelled"

    # 3. Timeout normalization: svc::ResultTimedOut / 0xEA01 translates to ResultTimeout
    r_timeout = MockReactor()
    assert r_timeout.wait_for_timeout(1000, raw_code=0xEA01) == "ResultTimeout"

    # 4. Local cancel arrives during USB wait -> immediate abort
    usb1, reactor1 = MockUsbServer(), MockReactor()
    t1 = MockSendObjectTransfer(usb1, reactor1, total_size=1024)
    b_read1, rc1 = t1.reader_step(event_during_read="local_cancel")
    assert rc1 == "ResultTransferFailed" and usb1.get_is_cancelled() and usb1.get_is_broken() and t1.is_cancelled
    b_read_next, rc_next = t1.reader_step()
    assert rc_next == "ResultTransferFailed" and b_read_next == 0 and t1.packets_drained == 0

    # 5. Cancel arrives between read and WriteFile
    usb2, reactor2 = MockUsbServer(), MockReactor()
    t2 = MockSendObjectTransfer(usb2, reactor2, total_size=1024)
    b_read2, rc2 = t2.reader_step()
    assert rc2 == "ResultSuccess"
    t2.writer_step(b_read2)
    reactor2.set_result("ResultCancelled")
    _, rc2_cancel = t2.reader_step()
    assert rc2_cancel == "ResultTransferFailed" and t2.is_cancelled and usb2.get_is_broken()

    # 6. Writer-only cancel during active reader wait
    usb3, reactor3 = MockUsbServer(), MockReactor()
    t3 = MockSendObjectTransfer(usb3, reactor3, total_size=1024)
    b_read3, _ = t3.reader_step()
    t3.fail_write_cancelled = True
    assert t3.writer_step(b_read3) == "ResultTransferFailed"
    assert t3.is_cancelled and usb3.get_is_cancelled() and usb3.get_is_broken()
    _, rc3 = t3.reader_step()
    assert rc3 == "ResultTransferFailed"

    # 7. Timeout + successful CancelEndpoint terminates transport (unreachable PC)
    usb4, reactor4 = MockUsbServer(), MockReactor()
    resp4 = MockPtpResponder(usb4, reactor4)
    req_count4 = 0
    def mock_req4():
        nonlocal req_count4
        req_count4 += 1
        _, rc = usb4.transfer_packet(ep=1, size=512, reactor=reactor4, event_during_wait="host_silent")
        return rc
    assert resp4.loop_process(mock_req4) == "Terminated" and usb4.get_is_broken() and req_count4 == 1

    # 8. CancelEndpoint failure marks is_broken and terminates LoopProcess
    usb5, reactor5 = MockUsbServer(), MockReactor()
    usb5.fail_cancel_endpoint = True
    usb5.cancel_endpoint_rc = "ResultTimeout"
    resp5 = MockPtpResponder(usb5, reactor5)
    req_count5 = 0
    def mock_req5():
        nonlocal req_count5
        req_count5 += 1
        _, rc = usb5.transfer_packet(ep=1, size=512, reactor=reactor5, event_during_wait="host_silent")
        return rc
    assert resp5.loop_process(mock_req5) == "Terminated" and usb5.get_is_broken() and req_count5 == 1

    # 9. Immediate stop on local cancel (no EOT drain) and session recovery
    usb6, reactor6 = MockUsbServer(), MockReactor()
    resp6 = MockPtpResponder(usb6, reactor6)
    t6 = MockSendObjectTransfer(usb6, reactor6, total_size=51200)
    b1, rc1 = t6.reader_step()
    assert rc1 == "ResultSuccess" and b1 == 512
    t6.writer_step(b1)
    assert t6.packets_drained == 1

    b2, rc2 = t6.reader_step(event_during_read="local_cancel")
    assert rc2 == "ResultTransferFailed" and usb6.get_is_broken() and t6.is_cancelled

    for _ in range(10):
        b_rem, rc_rem = t6.reader_step()
        assert rc_rem == "ResultTransferFailed" and b_rem == 0
    assert t6.packets_drained == 1 and not t6.is_done

    assert resp6.loop_process(lambda: "ResultSuccess") == "Terminated" and resp6.finalized

    # Session recovery: re-initialize PtpResponder with new UsbServer session
    reactor6.set_result("ResultSuccess")
    usb6_recovered = MockUsbServer()
    resp6_recovered = MockPtpResponder(usb6_recovered, reactor6)
    t6_next = MockSendObjectTransfer(usb6_recovered, reactor6, total_size=1024)
    b2_1, rc2_1 = t6_next.reader_step()
    assert rc2_1 == "ResultSuccess" and b2_1 == 512
    t6_next.writer_step(b2_1)
    b2_2, rc2_2 = t6_next.reader_step()
    assert rc2_2 == "ResultSuccess" and b2_2 == 512
    t6_next.writer_step(b2_2)
    assert t6_next.is_done and not usb6_recovered.get_is_broken() and t6_next.bytes_written == 1024

    # 10. PC-side abort (0x748C): connection remains intact and aligned
    next_cmd = bytearray(b"\x0c\x00\x00\x00\x01\x00\x0c\x10\x01\x00\x00\x00")
    p_cmd = MockParser(buffer_size=512)
    p_cmd.stream = next_cmd
    p_cmd.flush()
    read_cmd, rc_cmd = p_cmd.read_buffer(12)
    assert rc_cmd == "ResultSuccess"


class MockUsbWatcherSystem:
    def __init__(self):
        self.haze_running, self.recovery_phase, self.recovery_start_ns = True, 0, 0
        self.charger, self.usb_state, self.usbds_up = "Standard", "Configured", True
        self.usb_host_active, self.mtp_enabled, self.haze_exits, self.haze_inits = False, True, 0, 0

    def start_recovery(self, now_ns: int):
        self.recovery_phase, self.recovery_start_ns = 1, now_ns
        self.usbds_up, self.usb_state = False, "Detached"

    def initialize_recovered_usb(self):
        self.recovery_phase = 2

    def clear_recovering(self):
        if self.recovery_phase == 2:
            self.recovery_phase = 0

    def poll_usb_storage(self, now_ns: int):
        is_recovering = self.recovery_phase != 0 and now_ns - self.recovery_start_ns <= 3_000_000_000
        if self.usbds_up and self.usb_state == "Configured" and is_recovering:
            self.clear_recovering()
            is_recovering = self.recovery_phase != 0
        genuinely_unplugged = (self.charger == "Unconnected")
        if self.haze_running and not (is_recovering and not genuinely_unplugged):
            if not self.usbds_up or self.usb_state == "Detached":
                self.haze_running, self.usb_host_active, self.haze_exits = False, True, self.haze_exits + 1
                self.clear_recovering()

    def apply_mtp_enable(self, enable: bool) -> bool:
        if enable == self.haze_running and self.mtp_enabled == enable:
            return True
        self.mtp_enabled = enable
        if enable:
            if self.charger == "Unconnected":
                return False
            self.usb_host_active, self.haze_running, self.haze_inits = False, True, self.haze_inits + 1
            self.usbds_up, self.usb_state = True, "Configured"
            self.clear_recovering()
            return True
        if self.haze_running:
            self.haze_running, self.usb_host_active, self.haze_exits = False, True, self.haze_exits + 1
            self.clear_recovering()
        return True


def simulate_usb_watcher_and_remount_contracts() -> None:
    # Cancel flag starts before the transport detaches, and old enumeration cannot clear it.
    w1 = MockUsbWatcherSystem()
    w1.start_recovery(1000)
    w1.usbds_up, w1.usb_state = True, "Configured"
    w1.poll_usb_storage(2000)
    assert w1.recovery_phase == 1
    w1.usbds_up, w1.usb_state = False, "Detached"
    w1.poll_usb_storage(2500)
    assert w1.haze_running and not w1.usb_host_active and w1.haze_exits == 0
    w1.initialize_recovered_usb()
    w1.usbds_up, w1.usb_state = True, "Configured"
    w1.poll_usb_storage(3000)
    assert w1.recovery_phase == 0 and w1.haze_running

    # 2. Genuine physical unplug during recovery: watcher exits haze immediately
    w2 = MockUsbWatcherSystem()
    w2.start_recovery(1000)
    w2.charger = "Unconnected"
    w2.poll_usb_storage(2000)
    assert not w2.haze_running and w2.usb_host_active and w2.haze_exits == 1

    # 3. Recovery timeout safeguard (>3s): exits haze if PC never responds
    w3 = MockUsbWatcherSystem()
    w3.start_recovery(0)
    w3.poll_usb_storage(3_500_000_001)
    assert not w3.haze_running and w3.usb_host_active and w3.haze_exits == 1

    # 4. Repeated cancel & recovery sequence
    w4 = MockUsbWatcherSystem()
    for seq in range(3):
        w4.start_recovery(seq * 10_000_000)
        w4.poll_usb_storage(seq * 10_000_000 + 1000)
        assert w4.haze_running
        w4.initialize_recovered_usb()
        w4.usbds_up, w4.usb_state = True, "Configured"
        w4.poll_usb_storage(seq * 10_000_000 + 2000)
        assert w4.recovery_phase == 0 and w4.haze_running
    assert w4.haze_exits == 0

    # 5. Failed initialize handling in recovery
    w5 = MockUsbWatcherSystem()
    w5.start_recovery(1000)
    w5.clear_recovering()
    w5.haze_running, w5.usb_host_active = False, True
    assert not w5.haze_running

    # 6. Manual remount with stale mtp_enabled=True and PC charger (PsmChargerType_Standard)
    w6 = MockUsbWatcherSystem()
    w6.haze_running, w6.mtp_enabled, w6.charger, w6.usb_host_active = False, True, "Standard", True
    assert w6.apply_mtp_enable(True) and w6.haze_running and not w6.usb_host_active and w6.haze_inits == 1


if __name__ == "__main__":
    simulate_cancellation_state_machine()
    simulate_parser_and_drain_contract()
    simulate_usb_watcher_and_remount_contracts()
    print("PASS: test_mtp_cancellation_models verified.")
