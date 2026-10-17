# Minimal setup.py. Its job: mark the wheel as platform-specific.
# Without it setuptools sees no ext_modules, tags the wheel "py3-none-any",
# and cibuildwheel aborts ("pure Python wheel was generated") -- or, worse,
# a py3-none wheel gets published and installs on Python versions it can't run on.
# With it the tag becomes e.g. cp313-cp313-linux_x86_64 (-> manylinux after auditwheel).
from setuptools import setup
from setuptools.dist import Distribution


class BinaryDistribution(Distribution):
    def has_ext_modules(self):
        return True


setup(distclass=BinaryDistribution)