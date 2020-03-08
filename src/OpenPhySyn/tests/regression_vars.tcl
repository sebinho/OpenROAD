# BSD 3-Clause License

# Copyright (c) 2019, SCALE Lab, Brown University
# All rights reserved.

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:

# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.

# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.

# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.

# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# Regression variables.

# Application program to run tests on.
set app "openroad"
set app_path [file join $openroad_dir "build" "src" $app]
# Application options.
set app_options "-no_init -no_splash -exit"
# Log files for each test are placed in result_dir.
set result_dir [file join $test_dir "results"]
# Collective diffs.
set diff_file [file join $result_dir "diffs"]
# File containing list of failed tests.
set failure_file [file join $result_dir "failures"]
# Use the DIFF_OPTIONS envar to change the diff options
# (Solaris diff doesn't support this envar)
set diff_options "-c"
if [info exists env(DIFF_OPTIONS)] {
  set diff_options $env(DIFF_OPTIONS)
}

set valgrind_suppress [file join $test_dir valgrind.suppress]
set valgrind_options "--num-callers=20 --leak-check=full --freelist-vol=100000000 --leak-resolution=high --suppressions=$valgrind_suppress"
if { [exec "uname"] == "Darwin" } {
  append valgrind_options " --dsymutil=yes"
}

proc cleanse_logfile { test log_file } {
  # Nothing to be done here.
}

################################################################

# Record a test in the regression suite.
proc record_test { test cmd_dir } {
  global cmd_dirs test_groups
  set cmd_dirs($test) $cmd_dir
  lappend test_groups(all) $test
  return $test
}

proc record_psn_tests { tests } {
  global test_dir
  foreach test $tests {
    # Prune commented tests from the list.
    if { [string index $test 0] != "#" } {
      record_test $test $test_dir
    }
  }
}

# Record tests in $STAX/designs.
proc record_test_design { tests } {
  global env
  if [info exists env(STAX)] {
    foreach dir_test $tests {
      # Prune commented tests from the list.
      if { [string index $dir_test 0] != "#" } {
	if {[regexp {([a-zA-Z0-9_]+)/([a-zA-Z0-9_]+)} $dir_test \
	       ignore cmd_subdir test]} {
	  set cmd_dir [file join $env(STAX) "designs" $cmd_subdir]
	  record_test $test $cmd_dir
	} else {
	  puts "Warning: could not parse test name $dir_test"
	}
      }
    }
  }
}

################################################################

proc define_test_group { name tests } {
  global test_groups
  set test_groups($name) $tests
}

proc group_tests { name } {
  global test_groups
  return $test_groups($name)
}

# Clear the test lists.
proc clear_tests {} {
  global test_groups
  unset test_groups
}

proc list_delete { list delete } {
  set result {}
  foreach item $list {
    if { [lsearch $delete $item] == -1 } {
      lappend result $item
    }
  }
  return $result
}

################################################################

# Regression test lists.

# Record tests in psn/test
record_psn_tests {
    # pinswap1
    # pinswap2
    # constprop1
    # constprop2
    # constprop3
    clone1
    clone2
    fanout1
    fanout2
}

# Record tests in $STAX/designs
record_test_design {
}

################################################################

# Regression test groups

# Medium speed tests.
# run time <15s with optimized compile
define_test_group med {
}

define_test_group slow {
}

set fast [group_tests all]
set fast [list_delete $fast [group_tests med]]
set fast [list_delete $fast [group_tests slow]]

define_test_group fast $fast