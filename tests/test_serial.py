"""
Small smoke tests using pytest.
"""

import os
import subprocess
import pytest
import dice_testing_utilities as dtu

dice_exe = "../../build/Dice"
main_test_dir = os.getcwd()


# This resets us to the main test directory BEFORE each test
@pytest.yield_fixture(autouse=True)
def dir_reset():
    os.chdir(main_test_dir)  # Go to main test directory
    yield  # Run test
    dtu.cleanup()  # Clean up Dice files


# Tests
def test_o2_stoc():

    os.chdir("./o2_stoc")
    subprocess.call(dice_exe, shell=True)
    dtu.test_energy()
    dtu.compare_2rdm()


def test_o2_det():

    os.chdir("./o2_det")
    subprocess.call(dice_exe, shell=True)
    dtu.test_energy()


def test_o2_det_trev():

    os.chdir("./o2_det_trev")
    subprocess.call(dice_exe, shell=True)
    dtu.test_energy()


def test_o2_det_trev_direct():

    os.chdir("./o2_det_trev_direct")
    subprocess.call(dice_exe, shell=True)
    dtu.test_energy()


def test_o2_det_direct():

    os.chdir("./o2_det_direct")
    subprocess.call(dice_exe, shell=True)
    dtu.test_energy()


def test_n2_pt_rdm():

    os.chdir("./n2_pt_rdm")
    subprocess.call(dice_exe, shell=True)
    # dtu.test_energy()
    # dtu.compare_2rdm(tol=5e-7)


def test_c2_pt_rdm():

    os.chdir("./c2_pt_rdm")
    subprocess.call(dice_exe, shell=True)
    # dtu.test_energy()
    dtu.compare_2rdm(tol=5e-7)
