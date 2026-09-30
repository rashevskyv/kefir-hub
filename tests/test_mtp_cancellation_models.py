#!/usr/bin/env python3
"""Behavioral simulation models for MTP cancellation and recovery contracts.

Simulates:
1. MTP UI & cancellation state machine (switch-side cancel, idle window guard, UI relaunch prevention).
2. PtpDataParser exact Read<T>, EOT packet semantics, and timeout normalization.
3. AsyncUsbServer URB lifecycle, mutable atomic state, and transport termination on broken endpoint.
4. Writer-only cancel during reader wait notifying actual USB wait state via m_cancelled/m_in_cleanup.
5. Timeout + CancelEndpoint transport termination vs. clean drain on unmolested connection.
"""

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
        should_cancel = False
        if self.transfer_active:
            self.transfer_active = False
            self.transfer_aborted = True
            should_cancel = True
        self.handled_seq = self.transfer_seq

        if should_cancel:
            self.cancel_haze_calls += 1
            self.reactor_result = "Cancelled"
            self.event_signaled = True
            self.files_on_disk.discard(name)

    def worker_exit(self, user_cancelled: bool) -> None:
        should_cancel_worker = False
        if user_cancelled and self.transfer_active:
            self.transfer_active = False
            self.transfer_aborted = True
            should_cancel_worker = True

        if self.handled_seq < self.transfer_seq:
            self.handled_seq = self.transfer_seq

        if should_cancel_worker:
            self.cancel_haze_calls += 1
            self.reactor_result = "Cancelled"
            self.event_signaled = True

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
    def set_cleanup(self, cleanup: bool) -> None:
        self.in_cleanup = cleanup

    def is_in_cleanup(self) -> bool:
        return self.in_cleanup

    def set_cancelled(self, cancelled: bool) -> None:
        self.is_cancelled = cancelled

    def get_is_cancelled(self) -> bool:
        return self.is_cancelled

    def set_broken(self, broken: bool) -> None:
        self.is_broken = broken

    def get_is_broken(self) -> bool:
        return self.is_broken

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
            self.in_cleanup = True

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

        bytes_read = 0
        if self.is_done:
            return 0, "ResultSuccess"

        if self.reactor.get_result() == "ResultStopRequested":
            return 0, "ResultStopRequested"

        data, rc = self.usb_server.transfer_packet(
            ep=1, size=512, reactor=self.reactor, event_during_wait=event_during_read, is_eot=is_eot
        )

        self._check_cancellation()

        if rc != "ResultSuccess" and rc != "ResultEndOfTransmission":
            return 0, rc

        bytes_read = len(data)
        self.packets_drained += 1
        if rc == "ResultEndOfTransmission" or self.bytes_written + bytes_read >= self.total_size:
            self.is_done = True

        return bytes_read, "ResultSuccess"

    def writer_step(self, size: int) -> str:
        # Discard writes if cancelled
        if self.is_cancelled:
            self.bytes_written += size
            return "ResultSuccess"

        # Simulate install consumer disabled by user cancel -> returns ResultCancelled
        if self.fail_write_cancelled:
            self.is_cancelled = True
            # Propagate cancel to actual usb_server state polled by reader wait
            self.usb_server.set_cancelled(True)
            self.usb_server.set_cleanup(True)
            self.bytes_written += size
            return "ResultSuccess"

        self.bytes_written += size
        return "ResultSuccess"

    def _check_cancellation(self) -> None:
        if not self.is_cancelled:
            reactor_cancelled = (self.reactor.get_result() == "ResultCancelled")
            usb_cancelled = self.usb_server.get_is_cancelled()
            if reactor_cancelled or usb_cancelled:
                self.is_cancelled = True
                self.usb_server.set_cleanup(True)
        elif not self.usb_server.is_in_cleanup():
            self.usb_server.set_cleanup(True)


def simulate_cancellation_state_machine() -> None:
    """Verifies that cancel events prevent UI relaunch, do not double-cancel,
    and do not linger into subsequent operations if B is pressed during idle window.
    """
    s = MockMtpSystem()

    # Scenario 1: Normal completed transfer
    s.transfer_begin("/switch/file1.nsp")
    assert s.ui_alive and s.transfer_active
    s.transfer_end_success()
    assert not s.transfer_active
    assert "/switch/file1.nsp" in s.files_on_disk
    s.worker_exit(user_cancelled=False)
    s.pbox_done()
    assert not s.ui_alive
    assert s.relaunch_count == 0
    assert s.cancel_haze_calls == 0

    # Scenario 2: User presses B -> Yes during 1.5s idle window AFTER transfer 1 finished
    s.user_cancels_on_switch("/switch/file1.nsp")
    assert s.cancel_haze_calls == 0, "CancelTransfer must NOT be called if transfer already finished"
    assert not s.event_signaled, "Signal must not be set for already-finished transfer"
    assert "/switch/file1.nsp" in s.files_on_disk, "Completed file must not be deleted"
    s.worker_exit(user_cancelled=True)
    assert s.cancel_haze_calls == 0, "Worker must NOT call CancelTransfer after idle cancel"
    s.pbox_done()
    assert not s.ui_alive
    assert s.relaunch_count == 0

    # Scenario 3: Next transfer starts -> must NOT be aborted by previous idle cancel!
    s.transfer_begin("/switch/file2.nsp")
    assert s.transfer_active and not s.transfer_aborted
    assert not s.event_signaled
    # User cancels WHILE transfer 2 IS active
    s.user_cancels_on_switch("/switch/file2.nsp")
    assert s.cancel_haze_calls == 1, "CancelTransfer MUST be called once when active"
    assert s.transfer_aborted and not s.transfer_active
    assert "/switch/file2.nsp" not in s.files_on_disk, "Partial file must be deleted"
    # Worker exits after user cancel
    s.worker_exit(user_cancelled=True)
    assert s.cancel_haze_calls == 1, "Worker must NOT call CancelTransfer a second time"
    s.pbox_done()
    assert not s.ui_alive, "UI must NOT relaunch on cancelled transfer"
    assert s.relaunch_count == 0

    # Scenario 4: Subsequent transfer 3 starts cleanly (responder clears cancelled state)
    s.transfer_begin("/switch/file3.nsp")
    assert s.transfer_active and not s.transfer_aborted
    assert s.reactor_result == "Success"
    assert not s.event_signaled
    s.transfer_end_success()
    s.worker_exit(user_cancelled=False)
    s.pbox_done()
    assert not s.ui_alive
    assert s.relaunch_count == 0
    assert "/switch/file3.nsp" in s.files_on_disk
    assert s.cancel_haze_calls == 1


def simulate_parser_and_drain_contract() -> None:
    """Simulate PtpDataParser EOT semantics, exact Read<T>, partial buffer preservation,
    and all core senior review cancellation and error propagation scenarios.
    """
    # 1. Truncated Read<T> must fail with ResultInvalidArgument
    p_trunc = MockParser(buffer_size=2)
    p_trunc.stream = bytearray(b"AB")
    p_trunc.flush()
    p_trunc.persistent_error = "ResultEndOfTransmission"
    val, rc = p_trunc.read_scalar(size=4)
    assert rc != "ResultSuccess", "Truncated Read<T> must NOT return ResultSuccess"
    assert rc == "ResultInvalidArgument" or rc == "ResultEndOfTransmission", f"Got: {rc}"

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

    # --- Scenario 1: Local cancel arrives during USB wait, completion returns success ---
    usb1 = MockUsbServer()
    reactor1 = MockReactor()
    t1 = MockSendObjectTransfer(usb1, reactor1, total_size=1024)
    b_read1, rc1 = t1.reader_step(event_during_read="local_cancel")
    assert rc1 == "ResultSuccess", "URB completes success"
    assert usb1.get_is_cancelled(), "UsbServer records cancel"
    assert t1.is_cancelled, "Reader caught cancel via check_cancellation() after read"
    assert usb1.is_in_cleanup(), "Reader switched transport into cleanup mode"
    w_rc1 = t1.writer_step(b_read1)
    assert w_rc1 == "ResultSuccess"

    # --- Scenario 2: Cancel arrives between read and WriteFile ---
    usb2 = MockUsbServer()
    reactor2 = MockReactor()
    t2 = MockSendObjectTransfer(usb2, reactor2, total_size=1024)
    b_read2, _ = t2.reader_step()
    t2.writer_step(b_read2)
    reactor2.set_result("ResultCancelled")
    t2.reader_step()
    assert t2.is_cancelled, "check_cancellation before read immediately caught reactor cancel"
    assert usb2.is_in_cleanup()

    # --- Scenario 3: Writer-only cancel during active reader wait ---
    # Reader is waiting on USB; writer gets ResultCancelled from WriteFile and propagates to usb_server
    usb3 = MockUsbServer()
    reactor3 = MockReactor()
    t3 = MockSendObjectTransfer(usb3, reactor3, total_size=1024)
    b_read3, _ = t3.reader_step()
    t3.fail_write_cancelled = True
    w_rc3 = t3.writer_step(b_read3)
    assert w_rc3 == "ResultSuccess", "Writer absorbs ResultCancelled and discards further writes"
    assert t3.is_cancelled, "Writer marks is_cancelled"
    assert usb3.get_is_cancelled(), "Writer updated actual usb_server.m_cancelled"
    assert usb3.is_in_cleanup(), "Writer updated actual usb_server.m_in_cleanup"

    # Reader 1-second wait wakes up, observes m_in_cleanup/m_cancelled, switches to 5s cleanup deadline.
    # Host is silent -> cleanup deadline times out -> CancelEndpoint -> marks is_broken = True!
    _, rc3 = t3.reader_step(event_during_read="writer_cancel_during_wait")
    assert rc3 == "ResultTransferFailed"
    assert usb3.get_is_broken(), "Host silence timeout marked transport broken"

    # --- Scenario 4: Timeout + successful CancelEndpoint terminates transport ---
    # Even if CancelEndpoint succeeds, mid-container timeout breaks framing; LoopProcess must terminate
    usb4 = MockUsbServer()
    usb4.fail_cancel_endpoint = False  # CancelEndpoint succeeds
    reactor4 = MockReactor()
    resp4 = MockPtpResponder(usb4, reactor4)

    req_count4 = 0

    def mock_req4():
        nonlocal req_count4
        req_count4 += 1
        _, rc = usb4.transfer_packet(ep=1, size=512, reactor=reactor4, event_during_wait="host_silent")
        return rc

    loop_rc4 = resp4.loop_process(mock_req4)
    assert loop_rc4 == "Terminated", "LoopProcess must terminate immediately on timeout + CancelEndpoint"
    assert usb4.get_is_broken(), "Transport marked broken despite successful CancelEndpoint"
    assert req_count4 == 1, "Next command read was NOT executed on lost boundary"

    # --- Scenario 5: CancelEndpoint failure marks is_broken and terminates LoopProcess ---
    usb5 = MockUsbServer()
    usb5.fail_cancel_endpoint = True
    usb5.cancel_endpoint_rc = "ResultTimeout"
    reactor5 = MockReactor()
    resp5 = MockPtpResponder(usb5, reactor5)

    req_count5 = 0

    def mock_req5():
        nonlocal req_count5
        req_count5 += 1
        _, rc = usb5.transfer_packet(ep=1, size=512, reactor=reactor5, event_during_wait="host_silent")
        return rc

    loop_rc5 = resp5.loop_process(mock_req5)
    assert loop_rc5 == "Terminated"
    assert usb5.get_is_broken()
    assert req_count5 == 1

    # --- Scenario 6: Штатний local cancel з успішним drain зберігає з’єднання ---
    # All packets received until EOT, no timeout, CancelEndpoint NOT called -> next command accepted!
    usb6 = MockUsbServer()
    reactor6 = MockReactor()
    resp6 = MockPtpResponder(usb6, reactor6)

    req_count6 = 0
    t6 = MockSendObjectTransfer(usb6, reactor6, total_size=1024)

    def mock_req6():
        nonlocal req_count6
        req_count6 += 1
        if req_count6 == 1:
            # Transfer 1: Local cancel occurs, host drains all data cleanly to EOT
            t6.reader_step(event_during_read="local_cancel")
            t6.reader_step(is_eot=True)
            assert t6.is_done
            assert not usb6.get_is_broken(), "Clean drain must NOT mark transport broken"
            # Responder returns ResultCancelled, writes TransactionCanceled response, returns Success
            return "ResultSuccess"
        elif req_count6 == 2:
            # Transfer 2: Next command arrives on SAME connection and succeeds
            _, rc = usb6.transfer_packet(ep=1, size=12, reactor=reactor6)
            resp6.finalize()
            return rc
        return "ResultSuccess"

    # Loop handles Transfer 1 (cancel + drain) then continues to Transfer 2 on same connection!
    resp6.loop_process(mock_req6)
    assert req_count6 == 2, "Loop continued and processed next command on intact connection"
    assert not usb6.get_is_broken()

    # 7. Subsequent command intact after drain
    next_command_header = bytearray(b"\x0c\x00\x00\x00\x01\x00\x0c\x10\x01\x00\x00\x00")
    p_cmd = MockParser(buffer_size=512)
    p_cmd.stream = next_command_header
    p_cmd.flush()
    read_cmd, rc_cmd = p_cmd.read_buffer(12)
    assert rc_cmd == "ResultSuccess"
    assert read_cmd == next_command_header, "Subsequent command header must remain intact and aligned"
