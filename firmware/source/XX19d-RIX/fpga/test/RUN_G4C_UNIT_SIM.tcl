# G4C unit simulation: isolated temporary Vivado project. Called in batch.
set root [file normalize [file join [file dirname [info script]] ..]]
if {![info exists ::G4C_SIM_DEST]} {error "G4C: missing explicit isolated simulation directory"}
set dest [file normalize $::G4C_SIM_DEST]
if {[file exists $dest]} {error "G4C: simulation directory already exists: $dest"}
file mkdir $dest
create_project G4C_CREDIT_SIM $dest -part xc7z020clg400-1
add_files -fileset sim_1 [file join $root rtl XACPCommandReadCredit.v]
add_files -fileset sim_1 [file join $root test tb_XACPCommandReadCredit.sv]
set_property top tb_XACPCommandReadCredit [get_filesets sim_1]
set_property target_simulator XSim [current_project]
update_compile_order -fileset sim_1
launch_simulation -simset sim_1 -mode behavioral
run all
close_sim
puts "G4C_SIM_STAGE_FINISHED: inspect XSim log for G4C_CREDIT_TEST_PASS"
