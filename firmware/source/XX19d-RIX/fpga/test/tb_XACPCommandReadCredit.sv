`timescale 1ns/1ps
// Simulates an AXI command reader that stops consuming while the memory-side
// crossbar must complete Z/color reads. No full RasterIX/HP3 is emulated.
module tb_XACPCommandReadCredit;
    reg clk = 0;
    always #5 clk = ~clk;
    reg resetn = 0;
    reg [2:0] s_arid = 0;
    reg [31:0] s_araddr = 0;
    reg [7:0] s_arlen = 7;
    reg [2:0] s_arsize = 3;
    reg [1:0] s_arburst = 1;
    reg s_arlock = 0;
    reg [3:0] s_arcache = 3;
    reg [2:0] s_arprot = 0;
    reg s_arvalid = 0;
    wire s_arready;
    wire [2:0] s_rid;
    wire [63:0] s_rdata;
    wire [1:0] s_rresp;
    wire s_rlast,s_rvalid;
    reg s_rready = 0;

    wire [2:0] m_arid;
    wire [31:0] m_araddr;
    wire [7:0] m_arlen;
    wire [2:0] m_arsize;
    wire [1:0] m_arburst;
    wire m_arlock;
    wire [3:0] m_arcache;
    wire [2:0] m_arprot;
    wire m_arvalid;
    reg m_arready = 1;
    reg [2:0] m_rid = 0;
    reg [63:0] m_rdata = 0;
    reg [1:0] m_rresp = 0;
    reg m_rlast = 0;
    reg m_rvalid = 0;
    wire m_rready;

    XACPCommandReadCredit #(.ID_WIDTH(3),.ADDR_WIDTH(32),.DATA_WIDTH(64),.DEPTH_LOG2(6)) dut (
        .clk(clk),.resetn(resetn),
        .s_arid(s_arid),.s_araddr(s_araddr),.s_arlen(s_arlen),
        .s_arsize(s_arsize),.s_arburst(s_arburst),.s_arlock(s_arlock),
        .s_arcache(s_arcache),.s_arprot(s_arprot),.s_arvalid(s_arvalid),
        .s_arready(s_arready),.s_rid(s_rid),.s_rdata(s_rdata),
        .s_rresp(s_rresp),.s_rlast(s_rlast),.s_rvalid(s_rvalid),.s_rready(s_rready),
        .m_arid(m_arid),.m_araddr(m_araddr),.m_arlen(m_arlen),
        .m_arsize(m_arsize),.m_arburst(m_arburst),.m_arlock(m_arlock),
        .m_arcache(m_arcache),.m_arprot(m_arprot),.m_arvalid(m_arvalid),
        .m_arready(m_arready),.m_rid(m_rid),.m_rdata(m_rdata),
        .m_rresp(m_rresp),.m_rlast(m_rlast),.m_rvalid(m_rvalid),.m_rready(m_rready)
    );

    integer consumed = 0;
    integer accepted_ar = 0;
    integer accepted_mem = 0;
    integer cycle = 0;
    reg prior_ar_stall = 0;
    reg [31:0] prior_araddr;
    reg [7:0] prior_arlen;
    reg [2:0] prior_arid;

    always @(posedge clk) if (resetn) begin
        cycle = cycle+1;
        if (cycle > 1800) $fatal(1,"WATCHDOG timeout");
        if (m_arvalid && m_arready) accepted_ar = accepted_ar + 1;
        if (m_rvalid && m_rready) accepted_mem = accepted_mem + 1;
        if (s_rvalid && s_rready) begin
            if (s_rdata !== {32'd0,consumed[31:0]})
                $fatal(1,"RDATA mismatch at %0d: actual %h",consumed,s_rdata);
            if (s_rid !== (consumed/8) % 8)
                $fatal(1,"RID mismatch %0d",consumed);
            if (s_rresp !== ((consumed % 13 == 0) ? 2'b10 : 2'b00))
                $fatal(1,"RRESP mismatch %0d",consumed);
            if (s_rlast !== ((consumed % 8)==7))
                $fatal(1,"RLAST mismatch %0d",consumed);
            consumed = consumed + 1;
        end
        if (dut.reserved_count > 64) $fatal(1,"credit overflow");
        if ((dut.wr_ptr-dut.rd_ptr)>64) $fatal(1,"FIFO overflow");
        if ((dut.wr_ptr-dut.rd_ptr)>dut.reserved_count)
            $fatal(1,"more queued than reserved");
        if (prior_ar_stall && (!m_arvalid || m_araddr !== prior_araddr ||
            m_arlen !== prior_arlen || m_arid !== prior_arid))
            $fatal(1,"AXI ARVALID/ADDR unstable during !ARREADY");
        prior_ar_stall = m_arvalid && !m_arready;
        prior_araddr = m_araddr;
        prior_arlen = m_arlen;
        prior_arid = m_arid;
    end

    task automatic offer_ar(input integer burst_no);
        integer delay_ticks;
        begin
            @(negedge clk);
            s_arid = burst_no[2:0];
            s_araddr = 32'h067e7000 + burst_no*64;
            s_arlen = 7;
            s_arvalid = 1;
            delay_ticks = 0;
            do begin
                @(posedge clk);
                delay_ticks = delay_ticks + 1;
                if (delay_ticks>300) $fatal(1,"AR timeout burst %0d",burst_no);
            end while (!s_arready);
            @(negedge clk);
            s_arvalid = 0;
        end
    endtask

    task automatic provide_burst(input integer burst_no);
        integer b;
        begin
            for (b=0;b<8;b=b+1) begin
                @(negedge clk);
                m_rvalid=1;
                m_rid=burst_no[2:0];
                m_rdata=burst_no*8+b;
                m_rresp=(((burst_no*8+b)%13)==0)?2'b10:2'b00;
                m_rlast=(b==7);
                @(posedge clk);
                if (!m_rready) $fatal(1,"m_rready deasserted during blocked command consumer");
            end
            @(negedge clk);
            m_rvalid=0;
            m_rlast=0;
        end
    endtask

    integer i;
    initial begin
        repeat(5) @(negedge clk);
        resetn=1;
        // ARVALID and ARADDR must stay stable under an AXI ARREADY stall.
        @(negedge clk);
        s_arid=0; s_araddr=32'h067e7000; s_arlen=7;
        s_arvalid=1; m_arready=0;
        repeat(7) begin
            @(posedge clk);
            if (!m_arvalid || s_arready) $fatal(1,"ARVALID stall test failed");
        end
        @(negedge clk);
        m_arready=1;
        @(posedge clk);
        if (!s_arready) $fatal(1,"AR handshake failed after stall");
        @(negedge clk);
        s_arvalid=0;
        provide_burst(0);
        // Mimic command stream backpressure while Z/color traffic needs HP3.
        for (i=1;i<8;i=i+1) begin
            offer_ar(i);
            provide_burst(i);
        end
        if (accepted_mem !== 64 || consumed !== 0 || accepted_ar !== 8)
            $fatal(1,"phase 1 setup unexpected: AR=%0d R=%0d consumed=%0d",accepted_ar,accepted_mem,consumed);
        // Exactly 64 response beats are waiting for the command consumer.
        // A ninth read MUST be blocked until a downstream beat is released.
        @(negedge clk);
        s_arid=0;
        s_araddr=32'h067e7200;
        s_arlen=7;
        s_arvalid=1;
        repeat(20) begin
            @(posedge clk);
            if (s_arready || m_arvalid) $fatal(1,"over-reservation on burst 9");
            if (!m_rready) $fatal(1,"RREADY blocked unrelated Z traffic");
        end
        @(negedge clk);
        s_rready=1;
        // The pending AR will be granted after enough FIFO space is released.
        while (accepted_ar != 9) @(negedge clk);
        @(negedge clk);
        s_arvalid=0;
        provide_burst(8);
        repeat(100) @(negedge clk);
        if (accepted_mem !== 72 || consumed !== 72 || accepted_ar !== 9)
            $fatal(1,"expected 72 beats, got AR=%0d input=%0d output=%0d",accepted_ar,accepted_mem,consumed);
        $display("G4C_CREDIT_TEST_PASS: 9 bursts / 72 beats, stalled consumer, full FIFO, RID/RRESP/RLAST order");
        $finish;
    end
endmodule
