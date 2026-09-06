const getParentElement = (element) => {
    return element.parentElement ? element.parentElement : element.parentNode.host;
};
export default getParentElement;
