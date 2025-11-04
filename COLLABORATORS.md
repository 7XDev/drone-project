# Managing Collaborator Permissions

This guide explains how to grant admin permissions to collaborators in this GitHub repository.

## Important Note

**Collaborator permissions cannot be configured through code or configuration files in the repository.** They must be managed through GitHub's web interface by a repository owner or admin.

## How to Grant Admin Permissions to Collaborators

Only repository **owners** or users with **admin permissions** can change collaborator permissions.

### Step-by-Step Instructions

1. **Navigate to Repository Settings**
   - Go to the repository page: `https://github.com/7XDev/drone-project`
   - Click on the **Settings** tab (you must be an owner or admin to see this)

2. **Access Collaborators & Teams Section**
   - In the left sidebar, click on **Collaborators and teams** (or just **Collaborators** for personal repositories)
   - You may be asked to confirm your password for security

3. **View Current Collaborators**
   - You'll see a list of all current collaborators and their permission levels
   - Permission levels include:
     - **Read**: Can view and clone the repository
     - **Write**: Can push to the repository
     - **Maintain**: Can manage the repository without access to sensitive actions
     - **Admin**: Full access to the repository, including settings and deletion

4. **Change a Collaborator's Permission Level**
   - For each collaborator you want to grant admin permissions:
     - Click on the dropdown menu next to their name (currently showing their permission level)
     - Select **Admin** from the dropdown
     - The change takes effect immediately

5. **Add New Collaborators with Admin Permissions**
   - Click the **Add people** button
   - Enter the GitHub username or email of the person you want to add
   - Click **Add [username] to this repository**
   - In the dropdown that appears, select **Admin**
   - Click **Add [username] to drone-project**

## Alternative: Using GitHub Teams (For Organizations)

If this repository is part of a GitHub Organization, you can manage permissions more efficiently using teams:

1. **Create or Edit a Team**
   - Go to your organization's page
   - Click on **Teams**
   - Create a new team or edit an existing one

2. **Add Members to the Team**
   - Add all the collaborators you want to have admin permissions to this team

3. **Grant Team Admin Access to Repository**
   - Go back to the repository settings
   - Click on **Collaborators and teams**
   - Click on **Add teams**
   - Select your team and grant it **Admin** permissions

## Security Considerations

⚠️ **Important:** Admin permissions grant full control over the repository, including:
- Ability to delete the repository
- Ability to change repository settings
- Ability to add/remove other collaborators
- Access to repository secrets and security settings
- Ability to modify protected branches

**Best Practices:**
- Only grant admin permissions to trusted collaborators
- Consider using **Write** or **Maintain** permissions for most collaborators
- Use **Admin** permissions sparingly for those who truly need full repository control
- Regularly review collaborator permissions and remove access when no longer needed

## Permissions Overview

| Permission Level | What They Can Do |
|-----------------|------------------|
| **Read** | View code, issues, pull requests; clone the repository |
| **Triage** | Read + manage issues and pull requests |
| **Write** | Read + push to the repository, manage issues and pull requests |
| **Maintain** | Write + manage repository settings (except sensitive actions) |
| **Admin** | Full access to repository, including settings, secrets, and deletion |

## Questions or Issues?

If you don't have permission to change collaborator settings:
- Contact the repository owner: @7XDev
- Ask them to grant you admin permissions first, or to grant admin permissions to other collaborators

## Additional Resources

- [GitHub Documentation: Managing Teams and Collaborators](https://docs.github.com/en/account-and-profile/setting-up-and-managing-your-personal-account-on-github/managing-access-to-your-personal-repositories/inviting-collaborators-to-a-personal-repository)
- [GitHub Documentation: Repository Permission Levels](https://docs.github.com/en/get-started/learning-about-github/access-permissions-on-github)
