# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
import os
import sys
import subprocess
# sys.path.insert(0, os.path.abspath('.'))

SOURCE_ROOT = os.path.join(__file__, '..', '..', '..')

PRIVATE = tags.has('private')
privacy = 'private' if PRIVATE else 'public'

ARCH = None
ARCH_LIST = [
	'x86_64',
	'aarch64',
]

for arch in ARCH_LIST:
	if tags.has(arch):
		ARCH = arch
		break
 
if not ARCH in ARCH_LIST:
	raise RuntimeError(str(ARCH) + ' is not a supported architecture')

# -- Project information -----------------------------------------------------

project = 'Ferro'
copyright = '2021, Ariel Abreu'
author = 'Ariel Abreu'

# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [
	'sphinx.directives.other',
	'breathe',
]

root_doc = 'index.' + ARCH

# Add any paths that contain templates here, relative to this directory.
templates_path = [
	'_templates',
]

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = []

for arch in ARCH_LIST:
	if arch == ARCH:
		continue
	exclude_patterns += [
		'**/' + arch + '/*',
		'**/*.' + arch + '.rst',
		'*.' + arch + '.rst',
	]

# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
html_theme = 'sphinx_rtd_theme'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = [
	'_static',
]

# -- Breathe configuration ---------------------------------------------------

breathe_projects = {
	'ferro_x86_64': os.path.abspath(os.path.join(SOURCE_ROOT, 'build', 'x86_64', 'kernel', 'doxygen', privacy, 'xml')),
	'ferro_x86_64_private': os.path.abspath(os.path.join(SOURCE_ROOT, 'build', 'x86_64', 'kernel', 'doxygen', 'private', 'xml')),
	'ferro_aarch64': os.path.abspath(os.path.join(SOURCE_ROOT, 'build', 'aarch64', 'kernel', 'doxygen', privacy, 'xml')),
	'ferro_aarch64_private': os.path.abspath(os.path.join(SOURCE_ROOT, 'build', 'aarch64', 'kernel', 'doxygen', 'private', 'xml')),
}
breathe_default_project = 'ferro_' + ARCH
breathe_domain_by_extension = {
	'h' : 'c',
}

