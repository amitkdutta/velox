# Velox Documentation

We use Sphinx to generate documentation and GitHub pages to publish it.
- Sphinx: https://pythonhosted.org/an_example_pypi_project/sphinx.html
- GitHub pages: https://pages.github.com/

## Building

To install Sphinx: `easy_install -U sphinx`

`sphinx-quickstart` command was used to generate the initial Makefile and config.

To build the documentation, e.g. generate HTML files from .rst files:

Run `make html` from velox/docs.

Navigate to
`velox/docs/_build/html/index.html` in your browser to view the documentation.

## Publishing

GitHub pages is configured to display the contents of the top-level docs directory
found in the gh-pages branch. The documentation is available at
https://facebookincubator.github.io/velox.

To publish updated documentation, copy the contents of the _build/html
directory to the top-level docs folder and push to gh-pages branch.

```
# Make sure 'main' is updated to the top of the tree.
# Make a new branch.
git checkout -b update-docs main

# Generate the documentation.
cd velox/docs && make html

# Copy documentation files to the top-level docs folder.
cp -R _build/html/* ../../docs

# Commit the changes.
git add ../../
git commit -m "Update documentation"

# Update gh-pages branch in the upstream.
# This will get the website updated.
git push -f upstream update-docs:gh-pages
```

## Monthly Updates
A report which shares the latest improvements and changes to Velox.
This report is generated manually with the help of the following commands and template.

To get the list of contributions in a specific month, from the Velox root directory
execute the following command:
```
git log --since='June 1 2024' --until='June 30 2024' --pretty=format:"%h%x09%an%x09%ad%x09%s"
```

The following template is used to write a monthly update report. The main sections
are as follows.
- Documentation
- Core Library
- Presto Functions
- Spark Functions
- Hive Connector
- Performance and Correctness
- Build System
- Credits

Below are a few monthly update guidelines.
- New monthly reports [must be added here](monthly-updates/). Each new report
  must be linked to the [main document](monthly-updates.rst).
- Each update should be in the imperative, present tense similar to a git commit.
- In sections where applicable, start new additions with `Add` followed by
  the fixes using `Fix`, followed by other updates.
- Group similar contributions together. Example: All Presto scalar functions can be
  combined in one line.
- Add a reference to the PR if the update requires more context.

### Credits
A list of contributors with their contribution count, name, and optional affiliation.
An affiliation is the name of the company they are associated with.
The `credits.py` program tries to automatically infer an affiliation from an email using the
`affiliations_map.txt` file. New entries can be added to this file.
An individual user can also add a custom affiliation in the `mailmap_base64` file.
Each entry is in the `.mailmap` format: `Name <Affiliation> <email>`.
A user must decode the file using base64 to add an entry and then encode it again as below.
```
base64 -D -i velox/docs/mailmap_base64 -o .mailmap
echo "Velox User <ABC Corp> <veloxuser@abccorp.com>" >> .mailmap
base64 -i .mailmap -o velox/docs/mailmap_base64  
```
The affiliation is empty if none is found.
To get the list of contributors in a specific month say June-2024, from the Velox root directory
execute the following command:
```
python velox/docs/credits.py --since "June 01 2024" --until "June 30 2024"
```
Duplicate entries for a user are possible if different e-mails are used. Such entries must be combined.
